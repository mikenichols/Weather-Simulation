# we're using the intel compiler due to an error with _intel_fast_memcpy
CC = icpc
CFLAGS = -g

INCLUDE = -I/u/home2/mykphyre/include -I/u/local/apps/netcdf/current/include
LINK = -L/u/home2/mykphyre/lib -L/u/local/apps/netcdf/current/lib/
LIBS = -lglut -lIL -lILU -lILUT -lshp -lnetcdf_c++ -lnetcdf -llapack

ingest: ingest.cpp
	$(CC) $(CFLAGS) ingest.cpp -o ingest $(INCLUDE) $(LINK) $(LIBS)

clean:
	rm ingest *.o
