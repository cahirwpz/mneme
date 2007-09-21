all:		mgr.pdf

%.eps: %.dia
	dia -e $@ $<

%.pdf: %.eps
	epstopdf $<

mgr.pdf:	mgr.tex stronicowanie.pdf linux-layout.pdf heapblkman.pdf blkman.pdf
	pdflatex $<

dist:	mgr.pdf
	@rm -f mgr.tar.gz
	@tar cvzf mgr.tar.gz	\
		mgr.{tex,pdf}	\
		*.dia		\
		Makefile	\
		src/Makefile	\
		$(shell find src -name *.[ch])

clean:
	rm -f mgr.{aux,log,ps,eps,dvi,pdf,toc}
	rm -f {linux-layout,stronicowanie,heapblkman,blkman}.{pdf,eps}
	rm -f *~ *.tmp

# vim: ts=8 sw=8
