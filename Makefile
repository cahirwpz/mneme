all:		mgr.pdf

%.eps: %.dia
	dia -e $@ $<

%.pdf: %.eps
	epstopdf $<

mgr.pdf:	mgr.tex stronicowanie.pdf linux-layout.pdf reaps.pdf blkman.pdf
	pdflatex $<

view:
	gv mgr.pdf

clean:
	rm -f mgr.{aux,log,ps,eps,dvi,pdf,toc}
	rm -f {linux-layout,stronicowanie,reaps,blkman}.{pdf,eps}
	rm -f *~ *.tmp
