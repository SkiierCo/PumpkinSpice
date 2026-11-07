#pragma once

#include <string>

// Applies the code patches listed in <theme>/code.txt to the running Wii U Menu, if it is a supported build.
// Lines (anything after # is ignored):
//   page1_hide_right_arrow                  launcher page 1: the right arrow stays usable but is not drawn
//   folder_footer_color                     folder name bar, settings and back corners take the folder color
//   plaza_offset <x> <z>                    Wara Wara Plaza icon ring and centre moved by (x, z)
//   plaza_ring_radius <x> <z>               icon ring radii (stock 190 180)
//   plaza_crowd_radius <outer> <outer2> <inner>   Mii crowd scatter radii (stock 150 300 50)
void ApplyCodePatches(const std::string &themePath);
