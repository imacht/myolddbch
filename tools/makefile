all: dbch-tool dbch-pcap

dbch-tool: dbch-tool.o
	gcc -o $@ $^

dbch-pcap: dbch-pcap.c
	gcc -o $@ $^

clean:
	rm -f *.o dbch-tool dbch-tool.exe dbch-pcap{,.exe}
