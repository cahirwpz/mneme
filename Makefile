all:		mgr.pdf

%.eps: %.dia
	dia -e $@ $<

%.pdf: %.eps
	epstopdf $<

PDFS	=	stronicowanie.pdf linux-layout.pdf heapblkman.pdf mmapman-ao.pdf blkman.pdf pageman.pdf frag-zew.pdf schemat.pdf

all:	mgr.pdf

full:	$(PDFS) mgr.toc mgr.bbl mgr.aux mgr.pdf

mgr.pdf: $(PDFS) mgr.tex
	pdflatex mgr.tex

mgr.toc: mgr.aux
	pdflatex mgr.tex

mgr.bbl: mgr.bib mgr.tex
	bibtex mgr
	pdflatex mgr.tex

mgr.aux: mgr.tex
	pdflatex mgr.tex

dist:	full
	@rm -f mgr.tar.gz
	@tar --verbose			\
		--exclude *.bin		\
		--exclude *.o		\
		--exclude .svn 		\
		--exclude .libs		\
		--create		\
		--gzip			\
		--file mgr.tar.gz	\
		mgr.{tex,pdf}		\
		*.dia			\
		Makefile		\
		src

nocite:
	@sed -nr 's/@\w+\{\ (.*),/\\nocite{\1}/p' mgr.bib | sort

comments:
	@sed -r '/COMMENT/,/^}/!d' mgr.bib

clean:
	rm -f mgr.{aux,bbl,blg,log,ps,eps,dvi,pdf,toc,out}
	rm -f {linux-layout,stronicowanie,heapblkman,blkman,mmapman-ao,pageman,frag-zew,schemat}.{pdf,eps}
	rm -f *~ *.tmp

.PHONY: cite

# vim: ts=8 sw=8
