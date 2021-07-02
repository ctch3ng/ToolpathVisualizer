# ToolpathVisualizer
A simple tool to visualize and generate gcode for variable width single layer toolpaths. Used for testing.

This tool was used in testing [libArachne](https://github.com/Ultimaker/libarachne) and [Variable width contouring](https://github.com/mfx-inria/Variable-width-contouring).
Both these libraries were used to produce files describing the locations and widths of extrusion paths for one single layer of a 3D print. 

# Installation Procedures

The following procedures are for a fresh installation of Ubuntu 20.04.2.0

```
sudo apt install build-essential 
sudo apt install cmake 
sudo apt install git
git clone https://github.com/ctch3ng/ToolpathVisualizer
sudo apt-get install libboost-dev
sudo apt install libpolyclipping-dev
sudo apt-get install libtclap-dev
cd ToolpathVisualizer
mkdir build
cd build
cmake ..
make
```
