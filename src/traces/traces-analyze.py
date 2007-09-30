#!/usr/bin/python
# -*- coding: utf-8 -*-
# vim: fileencoding=utf-8 encoding=utf-8

from struct import *

import sys, os, mmap, stat, pprint, math, string
import psyco
import biggles

# -----------------------------------------------------------------------------

def classify(n):
	if n < 8:
		n = 8

	s = 8
	n = (n + (s - 1)) & ~(s - 1)

	while (1.0 - ((n - s) + 1.0) / n) < 1.0 / 32.0:
		s = s * 2
	
	return s

# -----------------------------------------------------------------------------

def prepareLog():
	log = {}

	i = 8
	s = 8

	while s < (1 << 28):
		frag = 1.0 - ((i - s) + 1.0) / i

		while frag < 1.0 / 32.0:
			if i & (s + s - 1) != 0:
				break

			s = s * 2
			
			frag = 1.0 - ((i - s) + 1.0) / i

		log[i] = 0

		i += s
	
	return log

# -----------------------------------------------------------------------------

def processLog(name):
	logfd  = os.open(name, os.O_RDONLY)
	loglen = int(os.fstat(logfd)[stat.ST_SIZE])
	data   = mmap.mmap(logfd, loglen, mmap.MAP_PRIVATE, mmap.PROT_READ)

	# === create traces per pid:threadid and block histogram ==================

	i   = 0
	num = 0

	blkhist = {}
	trace   = {}

	while i < loglen:
		# 0: uint32_t msec
		# 1: uint16_t opcode
		# 2: uint16_t pid
		# 3: uint32_t thrid
		# 4: uint32_t result
		# 5: uint32_t args[0]
		# 6: uint32_t args[1]

		logline = unpack("IHHIIII", data.read(24))

		id   = "%d:$%.8x" % (logline[2], logline[3])

		# blkhist
		if not blkhist.has_key(id):
			blkhist[id] = prepareLog()

		if logline[1] == 1:
			size = logline[5]
			bin  = classify(size)

			size = float((size + (bin - 1)) & ~(bin - 1))

			if not blkhist[id].has_key(size):
				blkhist[id][size] = 0.0

			blkhist[id][size] += 1.0

		# threads
		if not trace.has_key(id):
			trace[id] = []

		# free
		if logline[1] == 0:
			trace[id].append([logline[0], logline[1], logline[5]])

		# malloc
		if logline[1] == 1:
			trace[id].append([logline[0], logline[1], logline[5], logline[4]])

		# realloc
		if logline[1] == 2:
			trace[id].append([logline[0], logline[1], logline[5], logline[6], logline[4]])

		# memalign
		if logline[1] == 3:
			trace[id].append([logline[0], logline[1], logline[5], logline[6], logline[4]])

		i   += 24
		num += 1

	data.close()

	# =========================================================================

	blksize  = {}
	blkcount = {}

	for id in trace.keys():
		if not blksize.has_key(id):
			blksize[id] = {}

		if not blkcount.has_key(id):
			blkcount[id] = {}

		block = {}

		threshold = [1<<6, 1<<12, 1<<15, 1<<32]
		used  = [0, 0, 0, 0]
		count = [0, 0, 0, 0]

		blksize[id][0]  = [0, 0, 0, 0]
		blkcount[id][0] = [0, 0, 0, 0]

		for line in trace[id]:
			msec = line[0]

			if not blksize[id].has_key(msec):
				blksize[id][msec] = [ used[i] for i in range(4) ]

			if not blkcount[id].has_key(msec):
				blkcount[id][msec] = [ count[i] for i in range(4) ]

			# free
			if line[1] == 0:
				ptr = line[2]

				try:
					for i in range(4):
						if block[ptr] < threshold[i]:
							used[i] -= block[ptr]
							count[i] -= 1

					del block[ptr]
				except KeyError:
					print "free@%d: block at address %x not found!" % (msec, ptr)

			# malloc
			if line[1] == 1:
				size = line[2]
				res  = line[3]

				if block.has_key(res):
					print "malloc@%d: block at address %x already exists!" % (msec, res)

				block[res] = size

				for i in range(4):
					if size < threshold[i]:
						used[i]  += size
						count[i] += 1

			# realloc
			if line[1] == 2:
				ptr  = line[2]
				size = line[3]
				res  = line[4]

				if (ptr != res) and block.has_key(res):
					print "realloc@%d: block at address %x already exists!" % (msec, res)

				try:
					for i in range(4):
						if block[ptr] < threshold[i]:
							used[i]  -= block[ptr]
							count[i] -= 1

					del block[ptr]
				except KeyError:
					print "realloc@%d: block at address %x not found!" % (msec, ptr)
			
				block[res] = size

				for i in range(4):
					if size < threshold[i]:
						used[i]  += size
						count[i] += 1

			# memalign
			if line[1] == 3:
				align = line[2]
				size  = line[3]
				res   = line[4]

				if block.has_key(ptr):
					print "memalign@%d: block at address %x already exists!" % (msec, res)

				block[res] = size

				for i in range(4):
					if size < threshold[i]:
						used[i]  += size
						count[i] += 1

			# update !
			blksize[id][msec]  = [ used[i] for i in range(4) ]
			blkcount[id][msec] = [ count[i] for i in range(4) ]

	# =========================================================================

	return blkhist, blksize, blkcount

# -----------------------------------------------------------------------------

def drawBlkHist(blkhist):
	# cut unnecessary sizes
	ss = blkhist.keys()
	ss.sort()

	while blkhist[ss[-1]] == 0:
		ss.pop()
	
	# calculate ranges & histogram
	xmax = ((len(ss) + 9) / 10) * 10
	ymax = 0

	ys = []

	for s in ss:
		if ymax < blkhist[s]:
			ymax = blkhist[s]

		y = blkhist[s]

		if y > 0:
			y = math.log(y, 10)

		ys.append(y)

	ymax = math.ceil(math.log(ymax, 10))

	# draw a plot
	p = biggles.FramedPlot()
	p.title = "Histogram rozmiar\\'ow blok\\'ow"

	p.xrange = (0, xmax)
	p.yrange = (0, ymax)

	p.x1.label = "rozmiar bloku"
	p.y1.label = "# allokacji"

	p.x1.tickdir = 1
	p.y1.tickdir = 1

	p.x2.draw_ticks = 0
	p.y2.draw_ticks = 0

	p.y1.ticks      = int(ymax) + 1
	p.y1.ticklabels = ["0"] + [ str(int(math.pow(10, i+1))) for i in range(int(ymax)) ]
	p.y1.subticks   = 9

	p.x1.ticks         = [0]
	p.x1.ticklabels    = ["$2^3$"]
	p.x1.draw_subticks = 0

	for n in range(len(ss)):
		if ss[n] < 64:
			continue

		v = math.log(ss[n], 2)

		if v == int(v):
			p.x1.ticks.append(n)
			p.x1.ticklabels.append("$2^{%d}$" % int(v))

	p.add(biggles.Histogram(ys, color = 0xFF0000))

	return p

# -----------------------------------------------------------------------------

def drawBlkSize(blksize):
	ts = blksize.keys()
	ts.sort()

	ys = [[], [], [], []]

	maxused = [0, 0, 0, 0]

	for i in range(4):
		for t in ts:
			ys[i].append(blksize[t][i])

			if maxused[i] < blksize[t][i]:
				maxused[i] = blksize[t][i]

	# cut down data
	i = 0
	j = 0
	n = len(ts)

	while i < n:
		if i % 10 != 0:
			del ts[j]

			for m in range(4):
				del ys[m][j]
		else:
			j += 1

		i += 1

	# draw a plot
	p = biggles.FramedPlot()
	p.title = "Zuzycie pamieci"

	p.xrange = (0, ts[-1])
	p.yrange = (0, maxused[-1])

	p.x1.label = "czas [ms]"
	p.y1.label = "# bajt\\'ow"

	p.x1.tickdir = 1
	p.y1.tickdir = 1
	p.x1.draw_subticks = 0
	p.y1.draw_subticks = 0

	p.x2.draw_ticks = 0
	p.y2.draw_ticks = 0
	
	chart = [ None for i in range(4) ]

	chart[0] = biggles.FillBetween(ts, ys[2], ts, ys[3], color = 0x0060C0)	# all blocks
	chart[0].label = "bloki >= 32KiB"

	chart[1] = biggles.FillBetween(ts, ys[1], ts, ys[2], color = 0xFFC000)	# blocks <= 32kiB
	chart[1].label = "bloki [4KiB, 32KiB)"

	chart[2] = biggles.FillBetween(ts, ys[0], ts, ys[1], color = 0xF02020)	# blocks <= 4kiB
	chart[2].label = "bloki [64B, 4KiB)"

	chart[3] = biggles.FillBelow(ts, ys[0], color = 0x20F020)				# blocks <= 64B
	chart[3].label = "bloki < 64B"

	p.add(chart[0], chart[1], chart[2], chart[3])

	l = biggles.PlotKey(0.66, 0.4, [ chart[i] for i in range(4)])

	return p, l

# -----------------------------------------------------------------------------

def drawBlkCount(blkcount):
	ts = blkcount.keys()
	ts.sort()

	ys = [[], [], [], []]

	maxused = [0, 0, 0, 0]

	for i in range(4):
		for t in ts:
			ys[i].append(blkcount[t][i])

			if maxused[i] < blkcount[t][i]:
				maxused[i] = blkcount[t][i]

	# cut down data
	i = 0
	j = 0
	n = len(ts)

	while i < n:
		if i % 10 != 0:
			del ts[j]

			for m in range(4):
				del ys[m][j]
		else:
			j += 1

		i += 1

	# draw a plot
	p = biggles.FramedPlot()
	p.title = "Ilosc blok\\'ow"

	p.xrange = (0, ts[-1])
	p.yrange = (0, maxused[-1])

	p.x1.label = "czas [ms]"
	p.y1.label = "# blok\\'ow"

	p.x1.tickdir = 1
	p.y1.tickdir = 1
	p.x1.draw_subticks = 0
	p.y1.draw_subticks = 0

	p.x2.draw_ticks = 0
	p.y2.draw_ticks = 0

	chart = [ None for i in range(4) ]

	chart[0] = biggles.FillBetween(ts, ys[2], ts, ys[3], color = 0x0060C0)	# all blocks
	chart[0].label = "$bloki \ge\,32KiB$"

	chart[1] = biggles.FillBetween(ts, ys[1], ts, ys[2], color = 0xFFC000)	# blocks <= 32kiB
	chart[1].label = "$bloki \in [\,4KiB,\,32KiB\,)$"

	chart[2] = biggles.FillBetween(ts, ys[0], ts, ys[1], color = 0xF02020)	# blocks <= 4kiB
	chart[2].label = "$bloki \in [\,64B,\,4KiB\,)$"

	chart[3] = biggles.FillBelow(ts, ys[0], color = 0x20F020)				# blocks <= 64B
	chart[3].label = "$bloki < 64B$"

	p.add(chart[0], chart[1], chart[2], chart[3])

	l = biggles.PlotKey(0.63, 0.4, [ chart[i] for i in range(4)])

	return p, l

# -----------------------------------------------------------------------------

psyco.full()

blkhist, blksize, blkcount = processLog(sys.argv[1])

ks = blksize.keys()
ks.sort()

i = 0

for k in ks:
	chart_blkhist  = drawBlkHist(blkhist[k])
	chart_blksize, blksize_legend  = drawBlkSize(blksize[k])
	chart_blkcount, blkcount_legend = drawBlkCount(blkcount[k])

	chart_legend = biggles.Plot()
	chart_legend.xrange = (0.0, 1.0)
	chart_legend.yrange = (0.0, 1.0)
	chart_legend.add(blkcount_legend)
	chart_legend.add(biggles.Box((0.575, 0.525),(1.0, 0.133)))
	chart_legend.add(biggles.Label(0.68, 0.48, "Legenda:"))

	table = biggles.Table(2,2)
	table.aspect_ratio = 0.75

	table[0,0] = chart_blkhist
	table[0,1] = chart_blksize
	table[1,0] = chart_legend
	table[1,1] = chart_blkcount

	table.show()
	table.write_eps(sys.argv[1] + ".%d.eps" % i)

	i += 1
