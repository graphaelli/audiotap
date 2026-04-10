.PHONY: all clean

all: pcmplay

pcmplay: $(wildcard *.go)
	go build -o pcmplay .

clean:
	rm -f pcmplay
