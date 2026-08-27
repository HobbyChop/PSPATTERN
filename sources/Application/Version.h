#ifndef _PSPATTERN_VERSION_H_
#define _PSPATTERN_VERSION_H_

/* The one place the app's version lives.
 *
 * Keep this in step with the VERSION file at the repo root, which is
 * what tools/make_pspattern_dist.py names the release zip after. The
 * project screen used to carry its own hardcoded string, so a release
 * could ship announcing a version nobody had built. */

#define PSPATTERN_VERSION_STRING "v0.11.0a"

/* The product name is still provisional, so it lives here rather than
 * being typed into each screen. Renaming the app should be this line
 * plus the PSP_MODULE_INFO in PSPmain.cpp, and nothing else. */
#define PSPATTERN_NAME "PSPATTERN"

#endif
