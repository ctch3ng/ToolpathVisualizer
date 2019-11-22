
#include <cstdio>
#include <string.h>
#include <strings.h>
#include <sstream>
#include <stdio.h> // for file output
#include <fstream>
#include <iostream>
#include <algorithm> // random_shuffle

#include <boost/version.hpp>

#include <unordered_set>
#include <unordered_map>

#include <tclap/CmdLine.h> // command line argument parser

#include "utils/logoutput.h"
#include "utils/polygon.h"
#include "utils/gettime.h"
#include "utils/SVG.h"

#include "utils/VoronoiUtils.h"
#include "GcodeWriter.h"
#include "PathReader.h"
#include "Statistics.h"

#include "TestGeometry/Pika.h"
#include "TestGeometry/Jin.h"
#include "TestGeometry/Moessen.h"
#include "TestGeometry/Prescribed.h"
#include "TestGeometry/Spiky.h"
#include "TestGeometry/SVGloader.h"
#include "TestGeometry/TXTloader.h"
#include "TestGeometry/Microstructure.h"

#include "TestGeometry/VariableWidthGcodeTester.h"

using visualizer::Point;

namespace visualizer
{


static TCLAP::CmdLine gCmdLine(" Analyse toolpaths and generate single layer gcode ", ' ', "0.1");


static TCLAP::ValueArg<std::string> cmd__input_outline_filename("p", "polygon", "Input file for polygon", false /* required? */, "-", "path to file");
static TCLAP::ValueArg<std::string> cmd__input_segment_file("t", "toolpaths", "Input file for toolpaths", false /* required? */, "-", "path to file");
static TCLAP::ValueArg<std::string> cmd__output_prefix("o", "output", "Output file name prefix", false /* required? */, "TEST", "path to file");

static TCLAP::SwitchArg cmd__generate_gcodes("g", "gcode", "Generate gcode", false);
static TCLAP::SwitchArg cmd__generate_raft("", "raft", "Generate gcode for raft", false);
static TCLAP::SwitchArg cmd__generate_grid("", "grid", "Repeat gcode in grid with changing kappa setting", false);
static TCLAP::SwitchArg cmd__do_varWidthTest("", "varWidthTest", "generate width variation test", false);
static TCLAP::SwitchArg cmd__do_widthLimitsTest("", "widthLimitsTest", "generate width limits test spiral", false);


static TCLAP::SwitchArg cmd__analyse("a", "analyse", "Analyse output paths", false);
static TCLAP::ValueArg<double> cmd__scale_amount("", "scale", "Input polygon scaler", false /* required? */, 1.0, "floating number");
static TCLAP::SwitchArg cmd__process_even_toolpaths_only ("", "evenonly", "Only process even toolpaths", false);
static TCLAP::SwitchArg cmd__simplify_input_toolpaths("", "simplify", "Simplify input toolpaths to prevent firmware flooding", false);

static TCLAP::ValueArg<double> cmd__flow_modifier("", "flow", "Output extrusion flow scaler", false /* required? */, 1.0, "floating number");

std::string input_outline_filename = "";
std::string output_prefix = "";
std::string input_segment_file = "";
float input_outline_scaling = 1.0;

bool generate_gcode = true;
bool generate_raft = false;
bool generate_grid = false;
bool do_varWidthTest = false;
bool do_widthLimitsTest = false;

bool perform_analysis = false;

bool process_even_toolpaths_only = false;
bool simplify_input_toolpaths = false;

float flow_modifier = 0.9;

float nominal_print_speed = 30.0;
float travel_speed = 120.0;
coord_t layer_thickness = MM2INT(0.1);
float kappa = 1.1; // 0.25;//0.45;

float nominal_raft_speed = 50.0;



bool readCommandLine(int argc, char **argv)
{
    try {
        gCmdLine.add(cmd__input_outline_filename);
        gCmdLine.add(cmd__output_prefix);
        gCmdLine.add(cmd__input_segment_file);
        

        gCmdLine.add(cmd__generate_gcodes);
        gCmdLine.add(cmd__generate_raft);
        gCmdLine.add(cmd__generate_grid);
        gCmdLine.add(cmd__do_varWidthTest);
        gCmdLine.add(cmd__do_widthLimitsTest);
        
        
        gCmdLine.add(cmd__analyse);
        gCmdLine.add(cmd__scale_amount);
        gCmdLine.add( cmd__process_even_toolpaths_only );
        gCmdLine.add(cmd__simplify_input_toolpaths);

        gCmdLine.add(cmd__flow_modifier);

        gCmdLine.parse(argc, argv);

        input_outline_filename = cmd__input_outline_filename.getValue();
        output_prefix = cmd__output_prefix.getValue();
        input_segment_file = cmd__input_segment_file.getValue();

        generate_gcode = cmd__generate_gcodes.getValue();
        generate_raft = cmd__generate_raft.getValue();
        generate_grid = cmd__generate_grid.getValue();
        do_varWidthTest = cmd__do_varWidthTest.getValue();
        do_widthLimitsTest = cmd__do_widthLimitsTest.getValue();
        
        perform_analysis = cmd__analyse.getValue();
        
        
        input_outline_scaling = cmd__scale_amount.getValue();
        
        process_even_toolpaths_only = cmd__process_even_toolpaths_only.getValue();
        simplify_input_toolpaths = cmd__simplify_input_toolpaths.getValue();
        
        flow_modifier = cmd__flow_modifier.getValue();
        
        return false;
    }
    catch (const TCLAP::ArgException & e) {
        std::cerr << "Error: " << e.error() << " for arg " << e.argId() << std::endl;
    } catch (...) { // catch any exceptions
        std::cerr << "Error: unknown exception caught" << std::endl;
    }
    return true;
}


void convertSvg2SmoothPathPlanningFormat(const Polygons polys)
{
	std::cerr << "0.3\n";
	std::cerr << "1.0\n";
	for (ConstPolygonRef poly : polys)
	{
		std::cerr << poly.size() << '\n';
		for (Point p : poly)
			std::cerr << INT2MM(p.X) << " " << INT2MM(p.Y) << '\n';
	}
}

void squareGridTest(const std::vector<std::list<ExtrusionLine>> & result_polylines_per_index, const std::vector<std::list<ExtrusionLine>> & result_polygons_per_index, const Polygons & polys, const std::string output_prefix)
{
	AABB aabb(polys);
	Point aabb_size = aabb.max - aabb.min;

    std::ostringstream ss;
    ss << "visualization/" << output_prefix << ".gcode";
	GcodeWriter gcode(ss.str(), GcodeWriter::type_UMS5, true, layer_thickness, nominal_raft_speed, travel_speed, flow_modifier, true);
	
	
	Point grid_shape(4,6);
	coord_t gap_dist = MM2INT(2);
	
	Polygons raft_outline; // = polys.offset(MM2INT(5.0), ClipperLib::jtRound);
	AABB raft_aabb = aabb;
	for ( int x = 0; x < grid_shape.X; x++ )
	for ( int y = 0; y < grid_shape.Y; y++ )
	{
		Point translation = Point(.5 * (2 * x - grid_shape.X) * (aabb_size.X + gap_dist), .5 * (2 * y - grid_shape.Y) * (aabb_size.Y + gap_dist));
		raft_aabb.include(aabb.min + translation);
		raft_aabb.include(aabb.max + translation);
	}
	raft_aabb.expand(MM2INT(2.0));
	raft_outline = raft_aabb.toPolygons().offset(MM2INT(2.0), ClipperLib::jtRound);
	gcode.printRaft(raft_outline);

	gcode.switchExtruder(0);
//     gcode.marlin_estimates.reset(); gcode.total_naive_print_time = 0;
	gcode.setNominalSpeed(nominal_print_speed);
	
	gcode.printBrim(raft_aabb.toPolygons(), 1);
    gcode.retract();
	
	gcode.comment("TYPE:WALL-OUTER");
	float back_pressure_compensation = 0.0;
	for ( int x = 0; x < grid_shape.X; x++ )
	for ( int y = 0; y < grid_shape.Y; y++ )
	{
		Point translation = Point(.5 * (2 * x - grid_shape.X) * (aabb_size.X + gap_dist), .5 * (2 * y - grid_shape.Y) * (aabb_size.Y + gap_dist));
		gcode.setTranslation(translation);
		
		gcode.setBackPressureCompensation(back_pressure_compensation);
		gcode.comment("Back-pressure compensation: %f", back_pressure_compensation);
		
		
		gcode.comment("Pos:%i,%i", x, y);
		
// 		gcode.printBrim(aabb.toPolygons(), 1, MM2INT(0.4), MM2INT(0.6));
// 		gcode.retract();
		
		gcode.print(result_polygons_per_index, result_polylines_per_index, false);
		
		gcode.retract();
		back_pressure_compensation += 0.1;
	}
    
    std::cerr << "Print time: " << gcode.marlin_estimates.calculate() << "\n";
}

void raftedPrint(const std::vector<std::list<ExtrusionLine>> & result_polylines_per_index, const std::vector<std::list<ExtrusionLine>> & result_polygons_per_index, const Polygons & polys, const std::string output_prefix, bool brim = true)
{
	AABB aabb(polys);

    std::ostringstream ss;
    ss << "visualization/" << output_prefix << ".gcode";
	GcodeWriter gcode(ss.str(), GcodeWriter::type_UMS5, true, layer_thickness, nominal_raft_speed, travel_speed, flow_modifier, true);
	
	Polygons raft_outline = polys.offset(MM2INT(6.0), ClipperLib::jtRound).offset(MM2INT(-3.0), ClipperLib::jtRound);
	gcode.printRaft(raft_outline);
	gcode.retract();

	gcode.switchExtruder(0);
//     gcode.marlin_estimates.reset(); gcode.total_naive_print_time = 0;
	gcode.setNominalSpeed(nominal_print_speed);
	gcode.setBackPressureCompensation(kappa);
	gcode.comment("kappa: %f", kappa);
	gcode.retract();
	
// 	gcode.move(aabb.min);
    if (brim)
    {
        gcode.printBrim(polys, 1, MM2INT(0.4), MM2INT(0.6));
        gcode.retract();
    }
	
	gcode.comment("TYPE:WALL-OUTER");
	gcode.print(result_polygons_per_index, result_polylines_per_index, false, false);
    
    std::cerr << "Print time: " << gcode.marlin_estimates.calculate() << "\n";
}

void print(const std::vector<std::list<ExtrusionLine>> & result_polylines_per_index, const std::vector<std::list<ExtrusionLine>> & result_polygons_per_index, const Polygons & polys, const std::string output_prefix)
{
	AABB aabb(polys);

    std::ostringstream ss;
    ss << "visualization/" << output_prefix << ".gcode";
	GcodeWriter gcode(ss.str(), GcodeWriter::type_UM3, true, layer_thickness, nominal_raft_speed, travel_speed, flow_modifier, true);
	
	gcode.switchExtruder(0);
	gcode.setNominalSpeed(nominal_print_speed);
	gcode.setBackPressureCompensation(kappa);
	gcode.comment("kappa: %f", kappa);
	gcode.retract();
	
// 	gcode.move(aabb.min);
// 	gcode.printBrim(polys, 1, MM2INT(0.4), MM2INT(0.6));
// 	gcode.retract();
	
	gcode.comment("TYPE:WALL-OUTER");
	gcode.print(result_polygons_per_index, result_polylines_per_index, false, false);
    
    std::cerr << "Print time: " << gcode.marlin_estimates.calculate() << "\n";
}

void varWidthTest(std::vector<std::list<ExtrusionLine>> & result_polylines_per_index, std::vector<std::list<ExtrusionLine>> & result_polygons_per_index, Polygons & polys)
{
	result_polygons_per_index.clear();
	result_polylines_per_index.clear();
	
	result_polylines_per_index.emplace_back();
	result_polylines_per_index.back().emplace_back();
	ExtrusionLine & line = result_polylines_per_index.back().back();
	
	coord_t minW = MM2INT(0.3);
	coord_t maxW = MM2INT(0.7);
	coord_t midW = (minW + maxW) / 2;
	coord_t nrml = MM2INT(0.4);
	
	coord_t gap = MM2INT(0.7);
	
	coord_t endL = MM2INT(5);
	
	std::list<std::vector<Point>> dist_and_widths_list;
// 	dist_and_widths_list.emplace_back(std::initializer_list<Point>({Point(0,nrml), Point(endL * 2 + MM2INT(30),nrml)}));
	dist_and_widths_list.emplace_back(std::initializer_list<Point>({Point(0,nrml), Point(endL,minW), Point(MM2INT(30),maxW), Point(endL,nrml)}));
	dist_and_widths_list.emplace_back(std::initializer_list<Point>({Point(0,nrml), Point(endL,minW), Point(MM2INT(10),maxW), Point(MM2INT(10),minW), Point(MM2INT(10),maxW), Point(endL,nrml)}));
	dist_and_widths_list.emplace_back(std::initializer_list<Point>({Point(0,nrml), Point(endL,minW), Point(MM2INT(5),maxW), Point(MM2INT(5),minW), Point(MM2INT(5),maxW), Point(MM2INT(5),minW), Point(MM2INT(5),maxW), Point(MM2INT(5),minW), Point(endL,nrml)}));
	dist_and_widths_list.emplace_back(std::initializer_list<Point>({Point(0,nrml), Point(endL,maxW), Point(MM2INT(5),minW), Point(MM2INT(5),maxW), Point(MM2INT(5),minW), Point(MM2INT(5),maxW), Point(MM2INT(5),minW), Point(MM2INT(5),maxW), Point(endL,nrml)}));
	dist_and_widths_list.emplace_back(std::initializer_list<Point>({Point(0,nrml), Point(endL,maxW), Point(MM2INT(10),minW), Point(MM2INT(10),maxW), Point(MM2INT(10),minW), Point(endL,nrml)}));
	dist_and_widths_list.emplace_back(std::initializer_list<Point>({Point(0,nrml), Point(endL,maxW), Point(MM2INT(30),minW), Point(endL,nrml)}));
	dist_and_widths_list.emplace_back(std::initializer_list<Point>({Point(0,nrml), Point(endL * 2 + MM2INT(30),nrml)}));

	coord_t n_lines = 4;
	Point current_pos(0, 0);
	for ( const std::vector<Point> & dist_and_widths : dist_and_widths_list)
	{
		line.junctions.emplace_back(current_pos, dist_and_widths.front().Y);
		for ( size_t idx = 0; idx < dist_and_widths.size() ; idx++ )
		{
			if (idx > 0)
				current_pos.X += dist_and_widths[idx].X;
			line.junctions.emplace_back(current_pos, dist_and_widths[idx].Y);
		}
		current_pos.Y += maxW + gap;
		line.junctions.emplace_back(current_pos, nrml);
		current_pos.X = 0;
		line.junctions.emplace_back(current_pos, nrml);
		current_pos.Y += maxW + MM2INT(1.0);
	}

	AABB aabb;
	for ( ExtrusionJunction & j : line.junctions)
		aabb.include(j.p);
	aabb.expand(maxW / 2);
	polys = aabb.toPolygons();
}

void widthLimitsTest(std::vector<std::list<ExtrusionLine>> & result_polylines_per_index, std::vector<std::list<ExtrusionLine>> & result_polygons_per_index, Polygons & polys, coord_t target_w = MM2INT(0.1))
{
	result_polygons_per_index.clear();
	result_polylines_per_index.clear();
	
	result_polylines_per_index.emplace_back();
	result_polylines_per_index.back().emplace_back();
	ExtrusionLine & line = result_polylines_per_index.back().back();
	
	coord_t minW = MM2INT(0.3);
	coord_t maxW = MM2INT(1.2);
	coord_t nrml = MM2INT(0.4);
	
    coord_t sample_dist = MM2INT(0.4);
    
	coord_t gap = MM2INT(1.2);
	
	coord_t n_lines = 4;
    
    coord_t min_r = MM2INT(20.0);
    coord_t r = min_r;
    float max_a = 10.0 * 2*M_PI;
    
    float constant_cycles = 2.0 * 2*M_PI;
    
    Point prev(min_r, 0);
	for ( float a = 0; a < max_a; a += INT2MM(sample_dist) / INT2MM(r) )
	{
        Point current_pos(r * cos(a), r * sin(a));
        coord_t w = std::min(maxW, std::max(minW, coord_t(target_w - (target_w - nrml) * std::max(0.0f, a - constant_cycles) / (max_a - 2*constant_cycles))));
		line.junctions.emplace_back(current_pos, w);
        r = min_r + a / (2*M_PI) * gap;
        prev = current_pos;
	}

	Polygons polylines;
    PolygonRef polyline = polylines.newPoly();
	for ( ExtrusionJunction & j : line.junctions)
		polyline.add(j.p);
	polys = polylines.offsetPolyLine(gap).approxConvexHull(maxW - gap);
}

void test()
{
	bool is_svg = input_outline_filename.substr(input_outline_filename.find_last_of(".") + 1) == "svg";
    Polygons polys = is_svg? SVGloader::load(input_outline_filename) : TXTloader::load(input_outline_filename);
    polys.applyMatrix(PointMatrix::scale(input_outline_scaling));
    
    
    PathReader reader;
	if (reader.open(input_segment_file))
	{
		std::cerr << "Error opening " << input_segment_file << "!\n";
		std::exit(-1);
	}
    std::vector<std::list<ExtrusionLine>> result_polylines_per_index;
    std::vector<std::list<ExtrusionLine>> result_polygons_per_index;
	if (reader.read(result_polygons_per_index, result_polylines_per_index))
	{
		std::cerr << "Error reading " << input_segment_file << "!\n";
		std::exit(-1);
	}

	if (do_varWidthTest) varWidthTest(result_polylines_per_index, result_polygons_per_index, polys);
    if (do_widthLimitsTest) widthLimitsTest(result_polylines_per_index, result_polygons_per_index, polys);
	
	if (generate_gcode)
    {
        if (generate_grid)
        {
            squareGridTest(result_polylines_per_index, result_polygons_per_index, polys, output_prefix);
        }
        else if (generate_raft)
        {
            raftedPrint(result_polylines_per_index, result_polygons_per_index, polys, output_prefix);
        }
        else
        {
            print(result_polylines_per_index, result_polygons_per_index, polys, output_prefix);
        }
    }

    if (perform_analysis)
    {
        std::cout << "Computing statistics...\n";
        Statistics stats("external", output_prefix, polys, -1.0);
        stats.analyse(result_polygons_per_index, result_polylines_per_index);
        stats.visualize(MM2INT(0.3), MM2INT(0.9));
        stats.saveResultsCSV();
    }
}



} // namespace visualizer

int main(int argc, char *argv[])
{
    if( visualizer::readCommandLine(argc, argv) ) exit(EXIT_FAILURE);

    visualizer::test();
    return 0;
}
