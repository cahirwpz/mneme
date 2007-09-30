all:		mgr.pdf

%.eps: %.dia
	dia -e $@ $<

%.pdf: %.eps
	epstopdf $<

PDFS	=	stronicowanie.pdf linux-layout.pdf heapblkman.pdf mmapman-ao.pdf blkman.pdf pageman.pdf frag-zew.pdf

all:	mgr.pdf

mgr.pdf: $(PDFS) mgr.toc mgr.bbl mgr.aux 
	pdflatex mgr.tex

mgr.toc: mgr.aux
	pdflatex mgr.tex

mgr.bbl: mgr.tex mgr.bib
	bibtex mgr
	pdflatex mgr.tex

mgr.aux: mgr.tex
	pdflatex mgr.tex

dist:	mgr.pdf
	@rm -f mgr.tar.gz
	@tar cvzf mgr.tar.gz	\
		mgr.{tex,pdf}	\
		*.dia		\
		Makefile	\
		src/Makefile	\
		$(shell find src -name *.[ch])

nocite:
	@sed -nr 's/@\w+\{\ (.*),/\\nocite{\1}/p' mgr.bib | sort

clean:
	rm -f mgr.{aux,bbl,blg,log,ps,eps,dvi,pdf,toc,out}
	rm -f {linux-layout,stronicowanie,heapblkman,blkman,mmapman-ao,pageman,frag-zew}.{pdf,eps}
	rm -f *~ *.tmp

.PHONY: cite mgr.pdf

# vim: ts=8 sw=8
