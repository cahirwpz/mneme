all:		mgr.pdf

cpu-diagram.eps: cpu-diagram.dia
	dia -e $@ $<

%.pdf: %.eps
	epstopdf $<

mgr.pdf:	mgr.tex cpu-diagram.pdf
	pdflatex $<

view:
	gv mgr.pdf

clean:
	rm -f {mgr,cpu-diagram}.{aux,log,ps,eps,dvi,pdf,toc} *~
