all: histogram

histogram: histogram.cpp ppmb_io.a
	gcc -O3 $^ -o $@ -lm -lrt

ppmb_io.a: ppmb_io.o 
	ar rs $@ $<

.phony: clean

clean:
	rm -f ppmb_io.a ppmb_io.o histogram

