#pragma once
#include "package.h"
#include <string>

// BPS patches in a theme folder, used like a theme package: on the theme's first start each one is applied to this
// console's own Wii U Menu file, the finished file goes into the theme's content folder and the .bps is deleted, so
// from then on it's a regular theme. Two places count:
//   <theme>/Men.bps, Men2.bps, cafe_barista_men.bps   the way people usually share them (any case)
//   <theme>/content/.../<file>.bps                     any other file, next to where the finished file goes
//                                                      (e.g. content/UsEnglish/Message/AllMessage.szs.bps)
// Call before the theme's layers are added, so the console's own files are read as they are.

// True if the theme folder has a .bps to apply.
bool HasBpsPatches(const std::string &themePath);

// Applies every .bps of the theme. A patch that doesn't fit (made for another region or an already changed file:
// its source checksum doesn't match) or fails is left where it is to be tried again next start; *error gets the
// reasons. Returns true if every patch was applied. progress(file, files) is called as each one starts.
bool ApplyBpsPatches(const std::string &themePath, const PackageProgress &progress = nullptr, std::string *error = nullptr);
