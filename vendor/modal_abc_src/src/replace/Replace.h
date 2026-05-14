#ifndef __REPLACEmain__
#define __REPLACEmain__


#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>


#include "bookShelfIO.h"
#include "lefdefIO.h"
#include "bin.h"
#include "charge.h"
#include "fft.h"
#include "replace_private.h"
#include "initPlacement.h"
#include "macro.h"
#include "opt.h"
#include "plot.h"
#include "wlen.h"
#include "timing.h"

#include <tcl.h>

#include <math.h>

#include "base/main/main.h"
#include "map/mio/mio.h"
#include "base/io/ioAbc.h"

ABC_NAMESPACE_IMPL_START

#define    PL       1
#define coreHeight 1
#define termWidth  1
#define termHeight 1


int Replace_main(Abc_Ntk_t * pNtk, bool fFast, bool fIncre, bool fPrintOnly);
void ParseAbcNtk(Abc_Ntk_t * pNtk, bool fFast, bool fIncre);
void MapToAbcNtk(Abc_Ntk_t * pNtk, int fFast);

#endif