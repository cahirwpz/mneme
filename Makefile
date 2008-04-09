all:		mgr.pdf

%.eps: %.dia
	dia -e $@ $<

%.pdf: %.eps
	epstopdf $<

PDFS	=	paging.pdf linux-layout.pdf eqsbmgr.pdf eqsbmgr-sb.pdf mmapmgr.pdf blkmgr.pdf areamgr.pdf frag-ext.pdf schemat.pdf

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

lines:
	@find -maxdepth 1 -regextype posix-extended -regex "(.*\.(tex|bib)|Makefile)" | xargs cat | wc
	@find -maxdepth 1 -regextype posix-extended -regex "(.*\.(tex|bib)|Makefile)" | xargs cat | sed '/^$$/d' | wc

clean:
	rm -f mgr.{aux,bbl,blg,log,ps,eps,dvi,pdf,toc,out}
	rm -f $(PDFS)
	rm -f $(patsubst %.pdf,%.eps,$(PDFS))
	rm -f *~ *.tmp

.PHONY: cite

# vim: ts=8 sw=8
