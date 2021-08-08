.PHONY: all
all: evilwm.pdf evilwm.txt build.txt

evilwm.1: manpage-header evilwm.html
	(cat manpage-header; hxnormalize -xe -l 99999 evilwm.html | hxpipe | ./html2man.pl) > $@

build.1: manpage-header build.html
	(cat manpage-header; hxnormalize -xe -l 99999 build.html | hxpipe | ./html2man.pl) > $@

evilwm.pdf: evilwm.1
	./man2pdf.sh $< $@

evilwm.txt: evilwm.1
	./man2txt.sh $< $@

build.txt: build.1
	./man2txt.sh $< $@

.PHONY: all
clean:
	rm -f evilwm.1 evilwm.pdf evilwm.txt
	rm -f build.1 build.txt
