all: ssi

ssi: ssi.c
	gcc ssi.c -lreadline -lhistory -ltermcap -o ssi

clean:
	rm -f *.o
	rm -f ssi