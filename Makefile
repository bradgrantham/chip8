MINIFB_INCDIR   =       .
MINIFB_LIBDIR   =       .

CXXFLAGS        +=      -I$(MINIFB_INCDIR) -g

LDFLAGS         +=      -L$(MINIFB_LIBDIR) -lminifb -framework Metal -framework Cocoa  -framework IOKit -framework MetalKit

xochip: xochip.o
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)
