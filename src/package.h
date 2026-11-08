#pragma once
#include <functional>
#include <string>

// Theme packages (docs/PS-FORMAT.md): the theme's own work only. A package sits in its theme folder,
// <themes>/<name>/<anything>.ps (or next to it as <themes>/<name>.ps).

// If the console was turned off between moving a theme folder aside and putting the rebuilt one in place, puts the
// moved folder (with its package) back, so the package is rebuilt again. Call before listing themes.
void RecoverInterruptedPackages(const std::string &themesDir);

// The package of a theme, or "" if it has none.
std::string FindPackage(const std::string &themesDir, const std::string &name);

// The first start with a package rebuilds the finished files for this console's region from its own Menu files and
// makes them the regular theme folder <themes>/<name>: built in <name>.building; the folder that was there (the
// package, and an earlier version's files if any) is moved aside as <name>.replaced and then deleted.
// Returns the theme folder, or "" if the package could not be used (logged, and the reason in *error; nothing is
// changed and the package is tried again on the next start).
// progress(file, files) is called as each file of the package starts (files: this region's files).
using PackageProgress = std::function<void(int file, int files)>;
std::string PreparePackage(const std::string &themesDir, const std::string &name, const std::string &packagePath,
                           const PackageProgress &progress = nullptr, std::string *error = nullptr);
