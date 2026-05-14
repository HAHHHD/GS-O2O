///////////////////////////////////////////////////////////////////////////////
// Authors: Ilgweon Kang and Lutong Wang
//          (respective Ph.D. advisors: Chung-Kuan Cheng, Andrew B. Kahng),
//          based on Dr. Jingwei Lu with ePlace and ePlace-MS
//
//          Many subsequent improvements were made by Mingyu Woo
//          leading up to the initial release.
//
// BSD 3-Clause License
//
// Copyright (c) 2018, The Regents of the University of California
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
///////////////////////////////////////////////////////////////////////////////
#include "Replace.h"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>


#define compileDate __DATE__
#define compileTime __TIME__

prec globalWns;
prec globalTns;

prec netCut;
bool hasUnitNetWeight;
bool hasCustomNetWeight;
prec netWeight;
prec netWeightMin;
prec netWeightMax;

prec netWeightBase;
prec netWeightBound;
prec netWeightScale;
bool netWeightApply;

prec capPerMicron;
prec resPerMicron;

bool isClockGiven;
prec timingClock;
string clockPinName;

bool isInitSeed;
string plotColorFile;

int timingUpdateIter;
PIN *pinInstance;
MODULE *moduleInstance;
int pinCNT;
int moduleCNT;

string globalRouterPosition;
string globalRouterSetPosition;
prec globalRouterCapRatio;

// for moduleInst's pinName
vector< vector< string > > mPinName;

// for termInst's pinName
vector< vector< string > > tPinName;

vector<string> moduleNameStor;
vector<string> terminalNameStor;
vector<string> netNameStor;
vector<string> cellNameStor;

TERM *terminalInstance;
NET *netInstance;
HASH_MAP< string, int > netNameMap;
int terminalCNT;
int netCNT;

int gcell_cnt;
int row_cnt;
int place_st_cnt;

int gVerbose;

int STAGE;
int placementStdcellCNT;
int gfiller_cnt;
int placementMacroCNT;
int msh_yz;
int INPUT_FLG;
int gmov_mac_cnt;
int cGP3D_buf_iter;
int cGP2D_buf_iter;
int mGP3D_tot_iter;
int mGP2D_tot_iter;
int cGP3D_tot_iter;
int cGP2D_tot_iter;
int flg_3dic;
int flg_3dic_io;
int trial_iterCNT = 0;
int mGP3D_iterCNT = 0;
int mGP2D_iterCNT = 0;
int cGP3D_iterCNT = 0;
int cGP2D_iterCNT = 0;
int bloating_max_count = 1;
int bloatCNT = 0;
int potnPhaseDS;

// routability
prec h_pitch = 0.80;
prec v_pitch = 0.72;
prec max_inflation_ratio = 2.5;
prec inflation_ratio_coef = 1.0;
prec edgeadj_coef = 1.0;
prec pincnt_coef = 1.0;
prec gRoute_pitch_scal = 1.0;
prec ignoreEdgeRatio = 0.8;
prec inflation_threshold;
prec total_inflation_ratio;
prec h_max_inflation_ratio;
prec v_max_inflation_ratio;
prec total_inflatedNewAreaDelta;
bool isRoutabilityInit = false;
bool flg_noroute = false;
int routepath_maxdist;
int inflation_cnt = 0;
int inflation_max_cnt = 4;
prec inflation_area_over_whitespace = 0.25;
prec curr_filler_area = 0;
prec adjust_ratio = 0;
prec currTotalInflation;
bool is_inflation_h = false;
vector< pair< int, prec > > inflationList;

bool isTrial = false;
bool isFirst_gp_opt = true;
bool DEN_ONLY_PRECON;

int orderHPWL;
vector< pair< prec, prec > > trial_HPWLs;
vector< prec > trial_POTNs;
vector< pair< prec, prec > > hpwlEPs_1stOrder;
vector< pair< prec, prec > > hpwlEPs_2ndOrder;
vector< prec > potnEPs;

// .route
std::map< string, vector< int > > routeBlockageNodes;
int nXgrids, nYgrids, nMetLayers;
vector< int > verticalCapacity;
vector< int > horizontalCapacity;
vector< prec > minWireWidth;
vector< prec > minWireSpacing;
vector< prec > viaSpacing;
vector< tuple< int, int, int, int, int, int, int > > edgeCapAdj;
prec gridLLx, gridLLy;
prec tileWidth, tileHeight;
prec blockagePorosity;

prec gtsv_cof;
prec ALPHAmGP;
prec ALPHAcGP;
prec ALPHA;
prec BETAmGP;
prec BETAcGP;
prec BETA;
prec dampParam;
prec stn_weight;  // lutong
prec maxALPHA;
prec ExtraWSfor3D;
prec MaxExtraWSfor3D;
prec rowHeight;
prec SITE_SPA;

prec layout_area;
prec total_std_area;
prec total_std_den;
prec total_modu_area;
prec inv_total_modu_area;
prec total_cell_area;
prec curr_cell_area;  // lutong
prec total_term_area;
prec total_move_available_area;
prec total_filler_area;
prec total_PL_area;
prec total_termPL_area;  // mgwoo
prec total_WS_area;      // mgwoo
prec curr_WS_area;       // lutong
prec filler_area;
prec target_cell_den;
prec target_cell_den_orig;  // lutong
prec total_macro_area;
prec grad_stp;
prec gsum_phi;
prec gsum_ovfl;
prec gsum_ovf_area;
prec overflowMin;
prec mGP3D_opt_phi_cof;
prec mGP2D_opt_phi_cof;
prec cGP3D_opt_phi_cof;
prec cGP2D_opt_phi_cof;
prec inv_RAND_MAX;

unsigned extPt1_2ndOrder;
unsigned extPt2_2ndOrder;
unsigned extPt1_1stOrder;
unsigned extPt2_1stOrder;
unsigned extPt3_1stOrder;

char defOutput[BUF_SZ];
char defGpOutput[BUF_SZ];

char gbch_dir[BUF_SZ];
char gbch_aux[BUF_SZ];
char gbch[BUF_SZ];
char gGP_pl[BUF_SZ];
char gIP_pl[BUF_SZ];
char gGP_pl_file[BUF_SZ];
char gmGP2D_pl[BUF_SZ];
char gGP3_pl[BUF_SZ];
char gLG_pl[BUF_SZ];
char gDP_log[BUF_SZ];
char gDP_pl[BUF_SZ];
char gDP_tmp[BUF_SZ];
char gDP2_pl[BUF_SZ];
char gDP3_pl[BUF_SZ];
char gGR_dir[BUF_SZ];
char gGR_log[BUF_SZ];
char gGR_tmp[BUF_SZ];
char gFinal_DP_pl[BUF_SZ];
char bench_aux[BUF_SZ];
char dir_bnd[BUF_SZ];
char global_router[1023];
char output_dir[BUF_SZ];
char currentDir[BUF_SZ];

string sourceCodeDir;

// opt.cpp -> main.cpp
CELL *gcell_st;
ROW *row_st;

PLACE *place_st;
PLACE *place_backup_st;
PLACE place;
PLACE place_backup;
FPOS term_pmax;
FPOS term_pmin;
FPOS filler_size;
POS n1p;
POS msh;
FPOS gmin;
FPOS gmax;
TIER *tier_st;
POS dim_bin;
POS dim_bin_mGP2D;

// .shapes
// SHAPE's storage
vector< SHAPE > shapeStor;

// nodes String -> shapeStor's index.
HASH_MAP< string, vector< int > > shapeMap;
POS dim_bin_cGP2D;

///  ARGUMENTS  ///////////////////////////////////////////
int numLayer;
prec aspectRatio;
string bmFlagCMD;
string auxCMD;             // mgwoo
string defName;             // mgwoo
string sdcName;             // mgwoo
string verilogName;         // mgwoo
vector< string > libStor;  // mgwoo
string outputCMD;          // mgwoo
string experimentCMD;      // mgwoo
vector< string > lefStor;  // mgwoo
string verilogTopModule;
int defMacroCnt;
int numInitPlaceIter;
prec refDeltaWL;

int numThread;
InputMode inputMode;

string benchName;

string dimCMD;
string gTSVcofCMD;
string ALPHAmGP_CMD;
string ALPHAcGP_CMD;
string BETAmGP_CMD;
string BETAcGP_CMD;
string dampParam_CMD;
string aspectRatioCMD;
string stnweightCMD;    // lutong
string hpitchCMD;       // lutong
string vpitchCMD;       // lutong
string racntiCMD;       // lutong
string racntoCMD;       // lutong
string maxinflCMD;      // lutong
string inflcoefCMD;     // lutong
string edgeadjcoefCMD;  // lutong
string pincntcoefCMD;   // lutong
string routepath_maxdistCMD;
string gRoute_pitch_scalCMD;
string filleriterCMD;

// for detail Placer
int detailPlacer;
string detailPlacerLocation;
string detailPlacerFlag;

prec densityDP;
prec routeMaxDensity; 
bool hasDensityDP;
bool isSkipPlacement;
bool isOnlyLGinDP;
bool isPlot;
bool isSkipIP;
bool isBinSet;
bool isDummyFill;

int conges_eval_methodCMD;
bool isVerbose;
bool plotCellCMD;
bool plotMacroCMD;
bool plotDensityCMD;
bool plotFieldCMD;
bool constraintDrivenCMD;
bool isRoutability;
bool stnCMD;  // lutong
bool lambda2CMD;
bool dynamicStepCMD;
bool isOnlyGlobalPlace;
bool isTiming;
bool thermalAwarePlaceCMD;
bool trialRunCMD;
bool autoEvalRC_CMD;
bool onlyLG_CMD;
bool isFastMode;

bool fPrintOnlyG;
bool fReuseG;
///////////////////////////////////////////////////////////

Tcl_Interp* _interp;



extern "C" {
extern int Replace_Init(Tcl_Interp *interp);
}

using std::vector;
using std::tuple;

// // for *.nodes files
// struct NODES {
//   long index;
//   bool isTerminal;
//   bool isTerminalNI;
//   NODES(long _index, bool _isTerminal, bool _isTerminalNI)
//       : index(_index), isTerminal(_isTerminal), isTerminalNI(_isTerminalNI){};
//   NODES() : index(0), isTerminal(false), isTerminalNI(false){};
// };

using std::cout;
using std::endl;
using std::min;
using std::max;
using std::to_string;

extern FPOS grow_pmin, grow_pmax;
extern FPOS terminal_pmin, terminal_pmax;

extern int numNonRectangularNodes;
extern int totalShapeCount;
extern BookshelfNameMap _bsMap;

static FPOS terminal_size_max;
static FPOS module_size_max;
static POS max_mac_dim;

static HASH_MAP< string, NODES > nodesMap;

// *********************************
static HASH_MAP< Abc_Obj_t *, int > abcNodesMap;
static HASH_MAP< Abc_Obj_t *, int > abcTermsMap;

#define Epsilon 1.0E-15

////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

static Vec_Ptr_t * Io_NtkOrderingPads( Abc_Ntk_t * pNtk, Vec_Ptr_t * vTerms ); 
static Abc_Obj_t * Io_NtkBfsPads( Abc_Ntk_t * pNtk, Abc_Obj_t * pCurrEntry, unsigned numTerms, int * pOrdered );
static int Abc_NodeIsNand2( Abc_Obj_t * pNode );
static int Abc_NodeIsNor2( Abc_Obj_t * pNode );
static int Abc_NodeIsAnd2( Abc_Obj_t * pNode );
static int Abc_NodeIsOr2( Abc_Obj_t * pNode );
static int Abc_NodeIsXor2( Abc_Obj_t * pNode );
static int Abc_NodeIsXnor2( Abc_Obj_t * pNode );

static inline double Abc_Rint( double x )   { return (double)(int)x;  }


/**Function*************************************************************

  Synopsis    [Returns the closest I/O to a given I/O.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Abc_Obj_t * Io_NtkBfsPads( Abc_Ntk_t * pNtk, Abc_Obj_t * pTerm, unsigned numTerms, int * pOrdered )
{
    Vec_Ptr_t * vNeighbors = Vec_PtrAlloc ( numTerms ); 
    Abc_Obj_t * pNet, * pNode, * pNeighbor;
    int foundNeighbor=0;
    int i;

    assert(Abc_ObjIsPi(pTerm) || Abc_ObjIsPo(pTerm) );
    Abc_NtkIncrementTravId ( pNtk );
    Abc_NodeSetTravIdCurrent( pTerm );
    if(Abc_ObjIsPi(pTerm))
    {
    pNet = Abc_ObjFanout0(pTerm);
    Abc_ObjForEachFanout( pNet, pNode, i ) 
        Vec_PtrPush( vNeighbors, pNode );
    }
    else 
    {
    pNet = Abc_ObjFanin0(pTerm);
    Abc_ObjForEachFanin( pNet, pNode, i ) 
        Vec_PtrPush( vNeighbors, pNode );   
    }

    while ( Vec_PtrSize(vNeighbors) >0 )
    {
    pNeighbor = (Abc_Obj_t *)Vec_PtrEntry( vNeighbors, 0 );
    assert( Abc_ObjIsNode(pNeighbor) || Abc_ObjIsTerm(pNeighbor) );
    Vec_PtrRemove( vNeighbors, pNeighbor );

    if( Abc_NodeIsTravIdCurrent( pNeighbor ) )
        continue;
    Abc_NodeSetTravIdCurrent( pNeighbor );

    if( ((Abc_ObjIsPi(pNeighbor) || Abc_ObjIsPo(pNeighbor))) && !pOrdered[Abc_ObjId(pNeighbor)] )
    {
        foundNeighbor=1;
        break;
    }
    if( Abc_ObjFanoutNum( pNeighbor ) )
    {   
        pNet=Abc_ObjFanout0( pNeighbor );
        if( !Abc_NtkIsComb(pNtk) && Abc_ObjIsLatch(pNet) )
        pNet=Abc_ObjFanout0( Abc_ObjFanout0(pNet) );
        Abc_ObjForEachFanout( pNet, pNode, i )
        if( !Abc_NodeIsTravIdCurrent(pNode) ) 
            Vec_PtrPush( vNeighbors, pNode ); 
    }
    if( Abc_ObjFaninNum( pNeighbor ) )
    {
        if( !Abc_NtkIsComb(pNtk) && Abc_ObjIsLatch(Abc_ObjFanin0(pNeighbor)) )
        pNeighbor=Abc_ObjFanin0( Abc_ObjFanin0(pNeighbor) );
        Abc_ObjForEachFanin( pNeighbor, pNet, i )
        if( !Abc_NodeIsTravIdCurrent(pNode=Abc_ObjFanin0(pNet)) ) 
            Vec_PtrPush( vNeighbors, pNode );
    }
    }
    Vec_PtrFree(vNeighbors);
    return ( foundNeighbor ) ? pNeighbor : pTerm;
} 

/**Function*************************************************************

  Synopsis    [Returns the closest I/O to a given I/O.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Vec_Ptr_t * Io_NtkOrderingPads( Abc_Ntk_t * pNtk, Vec_Ptr_t * vTerms )
{ 
    ProgressBar * pProgress;
    unsigned numTerms=Vec_PtrSize(vTerms); 
    unsigned termIdx=0, termCount=0;
    int * pOrdered = ABC_ALLOC(int, numTerms + 1); // plus 1! #########################
    int newNeighbor=1;
    Vec_Ptr_t * vOrderedTerms = Vec_PtrAlloc ( numTerms );
    Abc_Obj_t * pNeighbor = NULL, * pNextTerm;     
    unsigned i; 

    for( i=0 ; i<numTerms + 1 ; i++ ) // plus 1! #########################
    pOrdered[i]=0; 
   
    pNextTerm = (Abc_Obj_t *)Vec_PtrEntry(vTerms, termIdx++);
    pProgress = Extra_ProgressBarStart( stdout, numTerms );
    for (i = 0; i < numTerms; ++i)
      for (int j = i + 1; j < numTerms; ++j)
        if (Abc_ObjId((Abc_Obj_t *)Vec_PtrEntry( vTerms, i )) == Abc_ObjId((Abc_Obj_t *)Vec_PtrEntry( vTerms, j )))
          printf("############%d, %d\n", i, j);
    while( termCount < numTerms ) //&& termIdx < numTerms )
    {
    if( pOrdered[Abc_ObjId(pNextTerm)] && !newNeighbor )
    {   
      if ( termIdx == numTerms )
        printf("*********%d\n", termCount);
        pNextTerm = (Abc_Obj_t *)Vec_PtrEntry( vTerms, termIdx++ );
        continue;
    }
    if(!Vec_PtrPushUnique( vOrderedTerms, pNextTerm ))
    {
        pOrdered[Abc_ObjId(pNextTerm)]=1;
        termCount++; 
    }
    pNeighbor=Io_NtkBfsPads( pNtk, pNextTerm, numTerms, pOrdered );
    if( (newNeighbor=!Vec_PtrPushUnique( vOrderedTerms, pNeighbor )) )
    {
       pOrdered[Abc_ObjId(pNeighbor)]=1;
       termCount++;
       pNextTerm=pNeighbor;
    }
    else if(termIdx < numTerms)
        pNextTerm = (Abc_Obj_t *)Vec_PtrEntry( vTerms, termIdx++ );

    Extra_ProgressBarUpdate( pProgress, termCount, NULL );
    }
    Extra_ProgressBarStop( pProgress );
    // printf("%d, %d\n", termCount, numTerms);
    assert(termCount==numTerms);
    ABC_FREE(pOrdered);
    return vOrderedTerms;
}

/**Function*************************************************************

  Synopsis    [Test is the node is nand2.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeIsNand2( Abc_Obj_t * pNode )
{
    Abc_Ntk_t * pNtk = pNode->pNtk;
    assert( Abc_NtkIsNetlist(pNtk) );
    assert( Abc_ObjIsNode(pNode) ); 
    if ( Abc_ObjFaninNum(pNode) != 2 )
        return 0;
    if ( Abc_NtkHasSop(pNtk) )
    return ( !strcmp(((char *)pNode->pData), "-0 1\n0- 1\n") || 
         !strcmp(((char *)pNode->pData), "0- 1\n-0 1\n") || 
         !strcmp(((char *)pNode->pData), "11 0\n") );
    if ( Abc_NtkHasMapping(pNtk) )
        return pNode->pData == (void *)Mio_LibraryReadNand2((Mio_Library_t *)Abc_FrameReadLibGen());
    assert( 0 );
    return 0;
}

/**Function*************************************************************

  Synopsis    [Test is the node is nand2.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeIsNor2( Abc_Obj_t * pNode )
{
    Abc_Ntk_t * pNtk = pNode->pNtk;
    assert( Abc_NtkIsNetlist(pNtk) );
    assert( Abc_ObjIsNode(pNode) ); 
    if ( Abc_ObjFaninNum(pNode) != 2 )
        return 0;
    if ( Abc_NtkHasSop(pNtk) )
        return ( !strcmp(((char *)pNode->pData), "00 1\n") );
    assert( 0 );
    return 0;
}

/**Function*************************************************************

  Synopsis    [Test is the node is and2.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeIsAnd2( Abc_Obj_t * pNode )
{
    Abc_Ntk_t * pNtk = pNode->pNtk;
    assert( Abc_NtkIsNetlist(pNtk) );
    assert( Abc_ObjIsNode(pNode) ); 
    if ( Abc_ObjFaninNum(pNode) != 2 )
        return 0;
    if ( Abc_NtkHasSop(pNtk) )
    return Abc_SopIsAndType(((char *)pNode->pData));
    if ( Abc_NtkHasMapping(pNtk) )
        return pNode->pData == (void *)Mio_LibraryReadAnd2((Mio_Library_t *)Abc_FrameReadLibGen());
    assert( 0 );
    return 0;
}

/**Function*************************************************************

  Synopsis    [Test is the node is or2.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeIsOr2( Abc_Obj_t * pNode )
{
    Abc_Ntk_t * pNtk = pNode->pNtk;
    assert( Abc_NtkIsNetlist(pNtk) );
    assert( Abc_ObjIsNode(pNode) ); 
    if ( Abc_ObjFaninNum(pNode) != 2 )
        return 0;
    if ( Abc_NtkHasSop(pNtk) )
    return ( Abc_SopIsOrType(((char *)pNode->pData))   || 
         !strcmp(((char *)pNode->pData), "01 0\n") ||
         !strcmp(((char *)pNode->pData), "10 0\n") ||
         !strcmp(((char *)pNode->pData), "00 0\n") );
         //off-sets, too
    assert( 0 );
    return 0;
}

/**Function*************************************************************

  Synopsis    [Test is the node is xor2.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeIsXor2( Abc_Obj_t * pNode )
{
    Abc_Ntk_t * pNtk = pNode->pNtk;
    assert( Abc_NtkIsNetlist(pNtk) );
    assert( Abc_ObjIsNode(pNode) ); 
    if ( Abc_ObjFaninNum(pNode) != 2 )
        return 0;
    if ( Abc_NtkHasSop(pNtk) )
    return ( !strcmp(((char *)pNode->pData), "01 1\n10 1\n") || !strcmp(((char *)pNode->pData), "10 1\n01 1\n")  );
    assert( 0 );
    return 0;
}

/**Function*************************************************************

  Synopsis    [Test is the node is xnor2.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeIsXnor2( Abc_Obj_t * pNode )
{
    Abc_Ntk_t * pNtk = pNode->pNtk;
    assert( Abc_NtkIsNetlist(pNtk) );
    assert( Abc_ObjIsNode(pNode) ); 
    if ( Abc_ObjFaninNum(pNode) != 2 )
        return 0;
    if ( Abc_NtkHasSop(pNtk) )
    return ( !strcmp(((char *)pNode->pData), "11 1\n00 1\n") || !strcmp(((char *)pNode->pData), "00 1\n11 1\n") );
    assert( 0 );
    return 0;
}




int Replace_main(Abc_Ntk_t *pNtk, bool fFast, bool fIncre, bool fPrintOnly) {

  // This forces the sequence of random numbers to be the same every run.
  srand(42);

  isFirst_gp_opt = true; //important!!!!!!!!!!!!!!
  
  double tot_cpu = 0;
  double time_ip = 0;
  double time_tp = 0;
  double time_mGP2D = 0;
  double time_mGP = 0;
  double time_lg = 0;
  double time_cGP2D = 0;
  double time_cGP = 0;
  double time_dp = 0;
  
  time_t rawtime;
  struct tm *timeinfo;

  time(&rawtime);
  timeinfo = localtime(&rawtime);
  total_hpwl.SetZero();

  // // 1. Clear Global Vectors
  // moduleNameStor.clear();
  // terminalNameStor.clear();
  // netNameStor.clear();
  // cellNameStor.clear();
  // mPinName.clear();
  // tPinName.clear();
  // inflationList.clear();
  // trial_HPWLs.clear();
  // trial_POTNs.clear();
  // hpwlEPs_1stOrder.clear();
  // hpwlEPs_2ndOrder.clear();
  // potnEPs.clear();

  // // 2. Clear Global Maps
  // nodesMap.clear();
  // abcNodesMap.clear();
  // abcTermsMap.clear();
  // shapeStor.clear();
  // shapeMap.clear();
  // routeBlockageNodes.clear();

  // // 3. Clear Route Global Vectors (if defined globally)
  // verticalCapacity.clear();
  // horizontalCapacity.clear();
  // minWireWidth.clear();
  // minWireSpacing.clear();
  // viaSpacing.clear();
  // edgeCapAdj.clear();

  // printCMD(argc, argv);
  cout << endl;
  PrintInfoString("CompileDate", compileDate);
  PrintInfoString("CompileTime", compileTime);
  PrintInfoString("StartingTime", asctime(timeinfo));

  ///////////////////////////////////////////////////////////////////////
  ///// Parse Arguments (defined in argument.cpp) ///////////////////////
  initGlobalVars();
  // if ( 1 )
  // {
  //   dim_bin.x = 256;
  //   dim_bin.y = 256;
  //   isBinSet = true;
  // }
  // if ( fIncre )
  // {
  //   overflowMin = 0.01;
  // }
  // else 
  // {
  //   overflowMin = 0.03;
  // }
  initGlobalVarsAfterParse();
  if (fIncre)
    INIT_LAMBDA_COF_GP = 0.1;
  cout << overflowMin << endl;
  // isPlot = 1;
  ///////////////////////////////////////////////////////////////////////

  ///////////////////////////////////////////////////////////////////////
  ///// Placement Initialization  ///////////////////////////////////////

  init();
  PrintProcBegin("Importing Placement Input");
  // ParseInput();
  ParseAbcNtk(pNtk, fFast, fIncre); //*****************************************************
  fPrintOnlyG = fPrintOnly;
  fReuseG = fFast || fIncre;

//  if(numLayer > 1)
//    calcTSVweight();

  net_update_init();
  init_tier();
  PrintProcEnd("Importing Placement Input");
  ///////////////////////////////////////////////////////////////////////

  time_start(&tot_cpu);
  // Normal cases
  if(!isSkipPlacement) {
    ///////////////////////////////////////////////////////////////////////
    ///// IP:  INITIAL PLACEMENT //////////////////////////////////////////
    PrintProcBegin("Initial Placement");
    time_start(&time_ip);
    build_data_struct(0);
    // initialPlacement_main();
    time_end(&time_ip);
    PrintProcEnd("Initial Placement");
    ///////////////////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////////////////
    ///// Setup before Placement Optimization  ////////////////////////////
    setup_before_opt();
    ///////////////////////////////////////////////////////////////////////

    time_start(&time_mGP);

    if(placementMacroCNT > 0) {
      ///////////////////////////////////////////////////////////////////////
      ///// mGP2D:  MIXED_SIZE_2D_GLOBAL_PLACE //////////////////////////////
    
      PrintProc("Begin Mixed-Size Global Placement ...");
      time_start(&time_mGP2D);
      mGP2DglobalPlacement_main();
      time_end(&time_mGP2D);

      printf("   RUNTIME(s) : %.4f\n\n\n", time_mGP2D);
      PrintProc("End Mixed-Size Global Placement");

      // WriteDef(defGpOutput);
      // WriteDef(defOutput);

      // no need to run any other flow with MS-RePlAce
      return 0;
      ///////////////////////////////////////////////////////////////////////
    }
    time_end(&time_mGP);

    time_start(&time_cGP);

    ///////////////////////////////////////////////////////////////////////
    ///// cGP2D:  STDCELL_ONLY_2D_GLOBAL_PLACE //////////////////////////////
    printf("PROC:  Standard Cell 2D Global Placement (cGP2D)\n");
    fflush(stdout);
    time_start(&time_cGP2D);
    cGP2DglobalPlacement_main();
    time_end(&time_cGP2D);
    time_end(&time_cGP);
    printf("   RUNTIME(s) : %.4f\n\n\n", time_cGP2D);
    fflush(stdout);
    printf("PROC:  END GLOBAL 2D PLACEMENT\n\n");
    fflush(stdout);
    ///////////////////////////////////////////////////////////////////////

    //    SavePlot( "Final GP Result");
    //    ShowPlot( benchName );
  }
  else {
    routeInst.Init();
    build_data_struct(false);
    tier_assign(STDCELLonly);
    setup_before_opt();
  }
  
  PrintUnscaledHpwl("GlobalPlacer");

  // Write BookShelf format
  // WriteBookshelf();

  if(isPlot) {
    SaveCellPlotAsJPEG("Final Global Placement Result", false,
                       string(dir_bnd) + string("/globalPlaceResult"));
  }


  ///////////////////////////////////////////////////////////////////////
  ///// GP:  DETAILED PLACEMENT /////////////////////////////////////////
  // if(isOnlyGlobalPlace && !fPrintOnly) {
  if(isOnlyGlobalPlace) {
    time_start(&time_dp);
    time_end(&time_dp);
  }
  else {
    time_start(&time_dp);
    STAGE = DETAIL_PLACE;

    CallDetailPlace();
    time_end(&time_dp);
  }
  time_end(&tot_cpu);
  ///////////////////////////////////////////////////////////////////////
  UpdateNetMinMaxPin2();
  // output_final_pl(gDP3_pl);
  if (fPrintOnly)
  {
    TIER *tier = &tier_st[0];
    if (pNtk->binsDensity)
    {
      for (int i = 0; i < tier->tot_bin_cnt; i++) {
        BIN *bp = &tier->bin_mat[i];
        // if (fabsf(bp->den2 - pNtk->binsDensity[i]) > 0.001)
        //   printf("ERROR!!!: %d, %f, %f\n", i, bp->den2, pNtk->binsDensity[i]);
      }
      float overflow = 0;
      for (int i = 0; i < pNtk->nBins; ++i)
      {
          // printf("%.2f && ", pNtk->binsDensity[i]);
          overflow += (pNtk->binsDensity[i] > 1) ? pNtk->binsDensity[i] - 1 : 0;
      }
      overflow *= pNtk->binModuleRatio;
      printf("\nOverflowFinal = %f, gSumOverflow = %f\n", overflow, gsum_ovfl);
    }
    else
    {
      printf("\ngSumOverflow = %f\n", gsum_ovfl);
      MapToAbcNtk(pNtk, 1);
    }
    return 0;
  }
  MapToAbcNtk(pNtk, fFast); //*********************************** 

  printf(" ### Numbers of Iterations: \n");
  if(trialRunCMD)
    printf("     Trial:  %d (%.4lf sec/iter)\n", trial_iterCNT,
           time_tp / (prec)trial_iterCNT);
  if(mGP2D_iterCNT != 0)
    printf("     mGP2D:  %d (%.4lf sec/iter)\n", mGP2D_iterCNT,
           time_mGP / ((prec)(mGP2D_iterCNT)));
  printf("     cGP2D:  %d (%.4lf sec/iter)\n", cGP2D_iterCNT,
         time_cGP / ((prec)(cGP3D_iterCNT + cGP2D_iterCNT)));
  printf("     ____________________________________\n");
  printf("     TOTAL:  %d\n\n", trial_iterCNT + mGP2D_iterCNT +
                                    cGP2D_iterCNT);

  printf(
      " ### CPU_{IP, TP, mGP, LG, cGP, DP} is %.2lf, %.2lf, %.2lf, %.2lf, "
      "%.2lf, %.2lf.\n",
      time_ip, time_tp, time_mGP, time_lg, time_cGP, time_dp);
  printf(" ### CPU_TOT is %.2lf seconds (%.2lf min).\n\n", tot_cpu,
         tot_cpu / 60);
  fflush(stdout);
  
  PrintUnscaledHpwl("Final");

  //    SavePlot( "Final Placement Result");
  if(isPlot) {
    SaveCellPlotAsJPEG("Final Placement Result", false,
                       string(dir_bnd) + string("/finalResult"));
  }

  // free_all();

  return 0;
}

void ParseAbcNtk(Abc_Ntk_t *pNtk, bool fFast, bool fIncre) {
  Abc_Obj_t * pLatch, * pNode;
  unsigned numTerms, numNodes;
  float coreCellArea=0;
  int i, k=0;
  
  Abc_Ntk_t * pNtkTemp = Abc_NtkToNetlist( pNtk );
  assert( Abc_NtkIsNetlist(pNtkTemp) );
  // Abc_Ntk_t * pNtkTemp = pNtk;

  // Abc_NtkForEachNode(pNtkTemp, pNode, i)
  // {
  //   while ((Abc_NtkObj(pNtk, k)) == __null || !Abc_ObjIsNode(Abc_NtkObj(pNtk, k)))
  //     k++;
  //   assert(pNode->xPos == Abc_NtkObj(pNtk, k)->xPos);
  //   assert(pNode->yPos == Abc_NtkObj(pNtk, k++)->yPos);
  // }

  // extract nodes #################################################
  // IO floating placement
  numTerms=0;
  // numTerms=Abc_NtkPiNum(pNtkTemp)+Abc_NtkPoNum(pNtkTemp);
  numNodes=numTerms+Abc_NtkNodeNum(pNtkTemp)+Abc_NtkLatchNum(pNtkTemp);
  printf("NumNodes : %d\t", numNodes );
  printf("NumTerminals : %d\n", numTerms );

  MODULE *maxSizeCell = NULL;
  MODULE *minSizeCell = NULL;

  MODULE *curModule = NULL;
  TERM *curTerminal = NULL;

  int buf_size = 255;
  int idx = 0;
  int totalInstanceCNT = 0;
  // int isTerminal = 0;
  // int isTerminalNI = 0;
  int first_term = 1;
  int first_modu = 1;

  totalInstanceCNT = moduleCNT = numNodes;
  terminalCNT = numTerms;
  moduleCNT -= terminalCNT;
  moduleInstance =
      (struct MODULE *)malloc(sizeof(struct MODULE) * moduleCNT);
  // std::vector<MODULE> moduleInstance(moduleCNT);

  if (!fIncre && ! fFast)
    terminalInstance =
        (struct TERM *)malloc(sizeof(struct TERM) * terminalCNT);
    // std::vector<TERM> terminalInstance(terminalCNT);

  // to find max size..
  prec maxSize = PREC_MIN;
  prec minSize = PREC_MAX;

  // cursor for (module, terminal) Instance
  int moduleCur = 0, terminalCur = 0;
  int cnt = 0;

  // max & min initialize
  FPOS maxCell(PREC_MIN, PREC_MIN);
  FPOS minCell(PREC_MAX, PREC_MAX);
  FPOS maxTerm(PREC_MIN, PREC_MIN);
  FPOS minTerm(PREC_MAX, PREC_MAX);

  char nodeName[255], line[LINESIZE];

  FPOS avgCellSize, avgTerminalSize;
  prec x, y;
  bool isTerminal = false, isTerminalNI = false;

  Abc_Obj_t * pTerm, * pNet;

  // IO floating placement: skip this
  // if (!fIncre && !fFast)
  // {
  //   // PI/POs
  //   Abc_NtkForEachPi( pNtkTemp, pTerm, i )
  //   {
  //     cnt++;
  //     pNet = Abc_ObjFanout0(pTerm);
  //     sprintf(nodeName, "i%s_input", Abc_ObjName(pNet));
  //     x = 1;
  //     y = 1;
  //     // get Index..
  //     idx = terminalCur++;
  //     isTerminal = true;
  //     // update NodesMap
  //     nodesMap[string(nodeName)] = NODES(idx, isTerminal, isTerminalNI);
  //     // terminalInstance update
  //     // both of terminal & terminal_NI are saved in here!
  //     curTerminal = &terminalInstance[idx];
  //     curTerminal->idx = idx;
  // //      strcpy(curTerminal->Name(), nodeName);
  //     terminalNameStor.push_back( nodeName );

  //     abcTermsMap[pTerm] = idx;

  //     curTerminal->size.x = x;
  //     curTerminal->size.y = y;

  //     // if Non-Image mode, ignore width & height
  //     if(isTerminalNI) {
  //       curTerminal->size.x = curTerminal->size.y = 0.0;
  //       curTerminal->isTerminalNI = true;
  //     }
  //     else {
  //       curTerminal->isTerminalNI = false;
  //     }

  //     curTerminal->area = curTerminal->size.x * curTerminal->size.y;

  //     // why this in here..
  //     curTerminal->pof = NULL;
  //     curTerminal->pin = NULL;
  //     curTerminal->netCNTinObject = 0;
  //     curTerminal->pinCNTinObject = 0;

  //     // max & min data update
  //     minTerm.x =
  //         (minTerm.x > curTerminal->size.x) ? curTerminal->size.x : minTerm.x;
  //     minTerm.y =
  //         (minTerm.y > curTerminal->size.y) ? curTerminal->size.y : minTerm.y;

  //     maxTerm.x =
  //         (maxTerm.x < curTerminal->size.x) ? curTerminal->size.x : maxTerm.x;
  //     maxTerm.y =
  //         (maxTerm.y < curTerminal->size.y) ? curTerminal->size.y : maxTerm.y;

  //     // update terminal_size_max (x,y)
  //     if(first_term) {
  //       first_term = 0;
  //       terminal_size_max = curTerminal->size;
  //     }
  //     else {
  //       terminal_size_max.x = max(curTerminal->size.x, terminal_size_max.x);
  //       terminal_size_max.y = max(curTerminal->size.y, terminal_size_max.y);
  //     }

  //     // average Terimanl Info update
  //     avgTerminalSize.x += curTerminal->size.x;
  //     avgTerminalSize.y += curTerminal->size.y;
  //   }
    
  //   Abc_NtkForEachPo( pNtkTemp, pTerm, i )
  //   {
  //     cnt++;
  //     pNet = Abc_ObjFanin0(pTerm);
  //     sprintf(nodeName, "o%s_output", Abc_ObjName(pTerm));
  //     x = 1;
  //     y = 1;
  //     // get Index..
  //     idx = terminalCur++;
  //     isTerminal = true;
  //     // update NodesMap
  //     nodesMap[string(nodeName)] = NODES(idx, isTerminal, isTerminalNI);
  //     // terminalInstance update
  //     // both of terminal & terminal_NI are saved in here!
  //     curTerminal = &terminalInstance[idx];
  //     curTerminal->idx = idx;
  // //      strcpy(curTerminal->Name(), nodeName);
  //     terminalNameStor.push_back( nodeName );

  //     abcTermsMap[pTerm] = idx;

  //     curTerminal->size.x = x;
  //     curTerminal->size.y = y;

  //     // if Non-Image mode, ignore width & height
  //     if(isTerminalNI) {
  //       curTerminal->size.x = curTerminal->size.y = 0.0;
  //       curTerminal->isTerminalNI = true;
  //     }
  //     else {
  //       curTerminal->isTerminalNI = false;
  //     }

  //     curTerminal->area = curTerminal->size.x * curTerminal->size.y;

  //     // why this in here..
  //     curTerminal->pof = NULL;
  //     curTerminal->pin = NULL;
  //     curTerminal->netCNTinObject = 0;
  //     curTerminal->pinCNTinObject = 0;

  //     // max & min data update
  //     minTerm.x =
  //         (minTerm.x > curTerminal->size.x) ? curTerminal->size.x : minTerm.x;
  //     minTerm.y =
  //         (minTerm.y > curTerminal->size.y) ? curTerminal->size.y : minTerm.y;

  //     maxTerm.x =
  //         (maxTerm.x < curTerminal->size.x) ? curTerminal->size.x : maxTerm.x;
  //     maxTerm.y =
  //         (maxTerm.y < curTerminal->size.y) ? curTerminal->size.y : maxTerm.y;

  //     // update terminal_size_max (x,y)
  //     if(first_term) {
  //       first_term = 0;
  //       terminal_size_max = curTerminal->size;
  //     }
  //     else {
  //       terminal_size_max.x = max(curTerminal->size.x, terminal_size_max.x);
  //       terminal_size_max.y = max(curTerminal->size.y, terminal_size_max.y);
  //     }

  //     // average Terimanl Info update
  //     avgTerminalSize.x += curTerminal->size.x;
  //     avgTerminalSize.y += curTerminal->size.y;
  //   }
  // }

  // latches
  if ( !Abc_NtkIsComb(pNtkTemp) )
    {
      Abc_NtkForEachLatch( pNtkTemp, pLatch, i )
      {
        cnt++;
        Abc_Obj_t * pNetLi, * pNetLo;
  
        pNetLi = Abc_ObjFanin0( Abc_ObjFanin0(pLatch) );
        pNetLo = Abc_ObjFanout0( Abc_ObjFanout0(pLatch) );
        /// write the latch line
        sprintf(nodeName, "%s_%s_latch", Abc_ObjName(pNetLi), Abc_ObjName(pNetLo));
        x = 6;
        y = 1;
        idx = moduleCur++;
        isTerminal = false;
        // update NodesMap
        nodesMap[string(nodeName)] = NODES(idx, isTerminal, isTerminalNI);
        // moduleInstance update
        //
        // if normal basic nodes...( not terminal & not terminalNI )
        curModule = &moduleInstance[idx];
        curModule->idx = idx;
    //      strcpy(curModule->Name(), nodeName);
        moduleNameStor.push_back(nodeName);

        abcNodesMap[pLatch] = idx;

        // width info
        // update size, half_size
        curModule->size.x = x;
        curModule->half_size.x = 0.5 * curModule->size.x;

        // height info
        // update size, half_size
        curModule->size.y = y;
        curModule->half_size.y = 0.5 * curModule->size.y;


        // update area
        curModule->area =
            curModule->size.x * curModule->size.y;

        // why this in here....
        curModule->pof = NULL;
        curModule->pin = NULL;
        curModule->netCNTinObject = 0;
        curModule->pinCNTinObject = 0;

        // max_min update for x.
        if(maxCell.x < curModule->size.x)
          maxCell.x = curModule->size.x;
        if(minCell.x > curModule->size.x)
          minCell.x = curModule->size.x;

        // max_min update for y
        if(maxCell.y < curModule->size.y)
          maxCell.y = curModule->size.y;
        if(minCell.y > curModule->size.y)
          minCell.y = curModule->size.y;

        // max update for area(size)
        if(maxSize < curModule->area) {
          maxSize = curModule->area;
          maxSizeCell = curModule;
        }

        // min update for area(size)
        if(minSize > curModule->area) {
          minSize = curModule->area;
          minSizeCell = curModule;
        }

        // update module_size_max (x,y)
        if(first_modu) {
          first_modu = 0;
          module_size_max = curModule->size;
        }
        else {
          module_size_max.x = max(module_size_max.x, curModule->size.x);
          module_size_max.y = max(module_size_max.y, curModule->size.y);
        }

        // for average calculation
        avgCellSize.x += curModule->size.x;
        avgCellSize.y += curModule->size.y;

        coreCellArea+=6*coreHeight;
      }
    }

  // internal node
  Abc_NtkForEachNode( pNtkTemp, pNode, i )
  {
    cnt++;
    // unsigned sizex=0, sizey=coreHeight, isize=0;
    float sizex=0, sizey=coreHeight, isize=0;
    //double nx, ny, xstep, ystep;    
    Abc_Obj_t * pNeti, *pNeto;
    int j;

    memset(nodeName, 0, sizeof(nodeName));

    // write the network after mapping 
    if ( Abc_NtkHasMapping(pNode->pNtk) )
    {
      Mio_Gate_t * pGate = (Mio_Gate_t *)pNode->pData;
      Mio_Pin_t * pGatePin;

      for ( pGatePin = Mio_GateReadPins(pGate), j = 0; pGatePin; pGatePin = Mio_PinReadNext(pGatePin), j++ )
        sprintf( nodeName + strlen(nodeName), "%s_", Abc_ObjName( Abc_ObjFanin(pNode,j) ) );
      assert ( j == Abc_ObjFaninNum(pNode) );
      sprintf( nodeName + strlen(nodeName), "%s_%s", Abc_ObjName( Abc_ObjFanout0(pNode) ), Mio_GateReadName(pGate) );
      sizex = Mio_GateReadArea(pGate);
      // sizex = ceil(Mio_GateReadArea(pGate));
    }
    else
    {
      Abc_ObjForEachFanin( pNode, pNeti, j )
          sprintf( nodeName + strlen(nodeName), "%s_", Abc_ObjName(pNeti) );
      Abc_ObjForEachFanout( pNode, pNeto, j )
          sprintf( nodeName + strlen(nodeName), "%s_", Abc_ObjName(pNeto) );      
      sprintf( nodeName + strlen(nodeName), "name" );

      isize=Abc_ObjFaninNum(pNode);
      if ( Abc_NodeIsConst0(pNode) || Abc_NodeIsConst1(pNode) ) 
          sizex=0;
      else if ( Abc_NodeIsInv(pNode) )
          sizex=1;
      else if ( Abc_NodeIsBuf(pNode) )
          sizex=2;
      else
      {
        assert( Abc_NtkHasSop(pNode->pNtk) );
        if ( Abc_NodeIsNand2(pNode) || Abc_NodeIsNor2(pNode) )
          sizex=2;
        else if ( Abc_NodeIsAnd2(pNode) || Abc_NodeIsOr2(pNode) )
            sizex=3;
        else if ( Abc_NodeIsXor2(pNode) || Abc_NodeIsXnor2(pNode) )
            sizex=5;
        else
        {
            // assert( isize > 2 );
            // sizex=isize+Abc_SopGetCubeNum((char *)pNode->pData);
            sizex = isize;
        }
      }
      // sizex = sizey = 1;
    }
    // int rd;
    // rd = rand();
    // sizex = rd - (rd / 2) * 2 + 1;
      x = sizex;
      y = sizey;

      // Equally place pins. Size pins needs /  isize+#output+1
      isize= isize + Abc_ObjFanoutNum(pNode) + 1;

      idx = moduleCur++;
      isTerminal = false;
      // update NodesMap
      nodesMap[string(nodeName)] = NODES(idx, isTerminal, isTerminalNI);
      // moduleInstance update
      //
      // if normal basic nodes...( not terminal & not terminalNI )
      curModule = &moduleInstance[idx];
      curModule->idx = idx;
  //      strcpy(curModule->Name(), nodeName);
      moduleNameStor.push_back(nodeName);

      abcNodesMap[pNode] = idx;

      // width info
      // update size, half_size
      curModule->size.x = x;
      curModule->half_size.x = 0.5 * curModule->size.x;

      // height info
      // update size, half_size
      curModule->size.y = y;
      curModule->half_size.y = 0.5 * curModule->size.y;


      // update area
      curModule->area =
          curModule->size.x * curModule->size.y;

      // why this in here....
      curModule->pof = NULL;
      curModule->pin = NULL;
      curModule->netCNTinObject = 0;
      curModule->pinCNTinObject = 0;

      // max_min update for x.
      if(maxCell.x < curModule->size.x)
        maxCell.x = curModule->size.x;
      if(minCell.x > curModule->size.x)
        minCell.x = curModule->size.x;

      // max_min update for y
      if(maxCell.y < curModule->size.y)
        maxCell.y = curModule->size.y;
      if(minCell.y > curModule->size.y)
        minCell.y = curModule->size.y;

      // max update for area(size)
      if(maxSize < curModule->area) {
        maxSize = curModule->area;
        maxSizeCell = curModule;
      }

      // min update for area(size)
      if(minSize > curModule->area) {
        minSize = curModule->area;
        minSizeCell = curModule;
      }

      // update module_size_max (x,y)
      if(first_modu) {
        first_modu = 0;
        module_size_max = curModule->size;
      }
      else {
        module_size_max.x = max(module_size_max.x, curModule->size.x);
        module_size_max.y = max(module_size_max.y, curModule->size.y);
      }

      // for average calculation
      avgCellSize.x += curModule->size.x;
      avgCellSize.y += curModule->size.y;

      coreCellArea += sizex * sizey;
  }
  if (!fIncre && !fFast)
    assert(cnt == totalInstanceCNT);
  else
    assert(cnt == moduleCNT);

  // finally divide - module-cells
  avgCellSize.x /= (prec)moduleCNT;
  avgCellSize.y /= (prec)moduleCNT;

  // finally divide - terminal
  avgTerminalSize.x /= (prec)terminalCNT;
  avgTerminalSize.y /= (prec)terminalCNT;

  printf("INFO:  PLACEMENT INSTANCES INCLUDING STDCELL and MOVABLE MACRO\n");
  printf("INFO:    Instance   MinX=%.2lf, MaxX=%.2lf\n", minCell.x, maxCell.x);
  printf("INFO:               MinY=%.2lf, MaxY=%.2lf\n", minCell.y, maxCell.y);
  printf("INFO:               AvgX=%.2lf, AvgY=%.2lf\n", avgCellSize.x,
         avgCellSize.y);
  printf("INFO:      Smallest Instance  %10s  Size %.2f\n", minSizeCell->Name(),
         minSize);
  printf("INFO:      Largest  Instance  %10s  Size %.2f\n", maxSizeCell->Name(),
         maxSize);

  printf("INFO:  TERMINALS INCLUDING PAD and FIXED MACRO\n");
  printf("INFO:    Terminal   MinX=%.2lf, MaxX=%.2lf\n", minTerm.x, maxTerm.x);
  printf("INFO:               MinY=%.2lf, MaxY=%.2lf\n", minTerm.y, maxTerm.y);
  printf("INFO:               AvgX=%.2lf, AvgY=%.2lf\n", avgTerminalSize.x,
         avgTerminalSize.y);
  max_mac_dim.x = maxCell.x;
  max_mac_dim.y = maxCell.y;


  // extract nets #################################################
  int netCur = 0;
  unsigned numPin = 0, numPinCur = 0;  
  
  int moduleID = 0;
  int io = 0;
  NET *curNet = NULL;
  PIN *pin = NULL;
  FPOS offset;
  Abc_Obj_t * pFanin, * pFanout;
  int j;

  // int isTerminal = 0;
  int max_net_deg = INT_MIN;
  
  // Abc_NtkForEachNet( pNtkTemp, pNet, i )
  //   numPin+=Abc_ObjFaninNum(pNet)+Abc_ObjFanoutNum(pNet);
  // netCNT = Abc_NtkNetNum(pNtkTemp);
  netCNT = 0;
  Abc_NtkForEachNet( pNtkTemp, pNet, i )
  {
    numPinCur = Abc_ObjFaninNum(pNet)+Abc_ObjFanoutNum(pNet);
    pFanin = Abc_ObjFanin0(pNet);
    if ( Abc_ObjIsPi(pFanin) )
      numPinCur--;
    Abc_ObjForEachFanout( pNet, pFanout, j )
      if ( Abc_ObjIsPo(pFanout) )
        numPinCur--;
    assert ( numPinCur >= 0 );
    if ( numPinCur > 1 )
    {
      netCNT++;
      numPin += numPinCur;
    }
  }
  pinCNT = numPin;

  netInstance = (struct NET *)malloc(sizeof(struct NET) * netCNT);
  pinInstance = (struct PIN *)malloc(sizeof(struct PIN) * pinCNT);
  // std::vector<NET> netInstance(netCNT);
  // std::vector<PIN> pinInstance(pinCNT);
  
  // offsets are simlply 0.00 0.00 at the moment
  offset.x = 0.0;
  offset.y = 0.0;

  int pid = 0;

// check the global HPWL before placement
int totalPins = 0, nPinsCur;
float totalHPWL = 0;
float xMin, xMax, yMin, yMax;
int ii, jj;
Abc_NtkForEachNet( pNtkTemp, pNet, ii )
{
    nPinsCur = 0;
    // IO floating mode: skip the terminals ****************************
    pFanin=Abc_ObjFanin0(pNet);
    if ( Abc_ObjIsPi(pFanin) )
    {
      xMin = 1000000;
      yMin = 1000000;
      xMax = -1000000;
      yMax = -1000000;
    }
    else
    {
      xMin = xMax = pFanin->xPos;
      yMin = yMax = pFanin->yPos;
      nPinsCur++;
    }
    Abc_ObjForEachFanout( pNet, pFanout, jj )
    {
        // IO floating mode: skip the terminals ****************************
        if ( Abc_ObjIsPo(pFanout) )
            continue;
        if (pFanout->Id == 0)
            continue;
        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
        nPinsCur++;
    }
    if ( xMin == 1000000 || nPinsCur == 1 )
      continue;
    totalPins += nPinsCur;
    totalHPWL += xMax - xMin + yMax - yMin;
}
printf("totalHPWLbeforePlacement = %f\n", totalHPWL);
std::cout << "totalPins = " << totalPins << std::endl;

  Abc_NtkForEachNet( pNtkTemp, pNet, i )
  {
    Abc_Obj_t * pNeti, * pNeto;
    Abc_Obj_t * pNetLi, * pNetLo, * pLatch;
    int k;
    int pinId = 0;
    int netPinCNT = 0;

    // compute the number of pins excluding terminals
    netPinCNT = Abc_ObjFaninNum(pNet)+Abc_ObjFanoutNum(pNet);
    pFanin=Abc_ObjFanin0(pNet);
    if ( Abc_ObjIsPi(pFanin) )
      netPinCNT--;
    Abc_ObjForEachFanout( pNet, pFanout, j )
      if ( Abc_ObjIsPo(pFanout) )
        netPinCNT--;
    if ( netPinCNT <= 1 )
      continue;

    idx = netCur++;
    curNet = &netInstance[idx];
    new(curNet) NET();
    curNet->idx = idx;
    curNet->pinCNTinObject = netPinCNT;

    netNameStor.push_back(Abc_ObjName(Abc_ObjFanin0(pNet))); 

    curNet->pin = (struct PIN **)malloc(
      sizeof(struct PIN *) * curNet->pinCNTinObject);

    if(max_net_deg < curNet->pinCNTinObject) {
      max_net_deg = curNet->pinCNTinObject;
    }

    pFanin=Abc_ObjFanin0(pNet);
    memset(nodeName, 0, sizeof(nodeName));
    if ( Abc_ObjIsPi(pFanin) )
    // IO floating placement
      isTerminal = 1;
    // sprintf( nodeName, "i%s_input", Abc_ObjName(pNet) );
    else 
    {
      if(!Abc_NtkIsComb(pNet->pNtk) && Abc_ObjFaninNum(pFanin) && Abc_ObjIsLatch(Abc_ObjFanin0(pFanin)) )
      {
        pLatch=Abc_ObjFanin0(pFanin);
        pNetLi=Abc_ObjFanin0(Abc_ObjFanin0(pLatch));
        pNetLo=Abc_ObjFanout0(Abc_ObjFanout0(pLatch));
        sprintf( nodeName, "%s_%s_latch", Abc_ObjName(pNetLi), Abc_ObjName(pNetLo) );
      }
      else
      {
        memset(nodeName, 0, sizeof(nodeName));
        Abc_ObjForEachFanin( pFanin, pNeti, k )
        sprintf( nodeName + strlen(nodeName), "%s_", Abc_ObjName(pNeti) );
        Abc_ObjForEachFanout( pFanin, pNeto, k )
        sprintf( nodeName + strlen(nodeName), "%s_", Abc_ObjName(pNeto) );    
        if ( Abc_NtkHasMapping(pNet->pNtk) )
        sprintf( nodeName + strlen(nodeName), "%s", Mio_GateReadName((Mio_Gate_t *)pFanin->pData) ); 
        else       
        sprintf( nodeName + strlen(nodeName), "name" );                           
      }
    }

    auto nodesMapPtr = nodesMap.find(string(nodeName));
    if(nodesMapPtr == nodesMap.end()) {
      // IO floating placement
      if ( !isTerminal )
      runtimeError(string(string(".net files' nodename is mismatched! : ") +
                          string(nodeName))
                        .c_str());
    }
    else {
      isTerminal = nodesMapPtr->second.isTerminal;
      moduleID = nodesMapPtr->second.index;
      // cout << "currentname: " << name << ", index: "
      // << nodesMapPtr->second.index << endl;
      assert (isTerminal == false);
    }
    // io = 1;

    // pin = &pinInstance[pid];
    // new(pin) PIN();
    // curNet->pin[0] = pin;

    // because we don't know, in the same nets,
    // what component goes into moduleInstance's pin or
    // terminalInstance's pin,
    //
    // It always reallocate all net's informations

    if(isTerminal == false) {
      io = 1;

      pin = &pinInstance[pid];
      // new(pin) PIN();
      curNet->pin[pinId++] = pin;

      curModule = &moduleInstance[moduleID];
      AddPinInfoForModuleAndTerminal(&curModule->pin, &curModule->pof,
                                      curModule->pinCNTinObject++, offset,
                                      moduleID, idx, 0, pid++, io, isTerminal);
    }
    else {
      // IO floating placement: skip this
      // curTerminal = &terminalInstance[moduleID];
      // AddPinInfoForModuleAndTerminal(&curTerminal->pin, &curTerminal->pof,
      //                                 curTerminal->pinCNTinObject++, offset,
      //                                 moduleID, idx, 0, pid++, io, isTerminal);
    }


    Abc_ObjForEachFanout( pNet, pFanout, j )
    {
      memset(nodeName, 0, sizeof(nodeName));
      if ( Abc_ObjIsPo(pFanout) )
      // IO floating placement
        isTerminal = 1; 
        // sprintf( nodeName, "o%s_output", Abc_ObjName(pFanout) );
      else
      {
        if(!Abc_NtkIsComb(pNet->pNtk) && Abc_ObjFanoutNum(pFanout) && Abc_ObjIsLatch( Abc_ObjFanout0(pFanout) ) )
        {
          pLatch=Abc_ObjFanout0(pFanout);
          pNetLi=Abc_ObjFanin0(Abc_ObjFanin0(pLatch));
          pNetLo=Abc_ObjFanout0(Abc_ObjFanout0(pLatch));
          sprintf( nodeName, "%s_%s_latch", Abc_ObjName(pNetLi), Abc_ObjName(pNetLo) );
        }
        else
        {
          memset(nodeName, 0, sizeof(nodeName));
          Abc_ObjForEachFanin( pFanout, pNeti, k )
          sprintf( nodeName + strlen(nodeName), "%s_", Abc_ObjName(pNeti) );
          Abc_ObjForEachFanout( pFanout, pNeto, k )
          sprintf( nodeName + strlen(nodeName), "%s_", Abc_ObjName(pNeto) );    
          if ( Abc_NtkHasMapping(pNet->pNtk) )
              sprintf( nodeName + strlen(nodeName), "%s", Mio_GateReadName((Mio_Gate_t *)pFanout->pData) ); 
          else
              sprintf( nodeName + strlen(nodeName), "name" );
        }
      }

      auto nodesMapPtr = nodesMap.find(string(nodeName));
      if(nodesMapPtr == nodesMap.end()) {
        // IO floating placement
        if ( !isTerminal )
        runtimeError(string(string(".net files' nodename is mismatched! : ") +
                            string(nodeName))
                          .c_str());
      }
      else {
        isTerminal = nodesMapPtr->second.isTerminal;
        moduleID = nodesMapPtr->second.index;
        // cout << "currentname: " << name << ", index: "
        // << nodesMapPtr->second.index << endl;
        assert (isTerminal == false);
      }
      // io = 0;

      // pin = &pinInstance[pid];
      // new(pin) PIN();
      // curNet->pin[j+1] = pin;

      // because we don't know, in the same nets,
      // what component goes into moduleInstance's pin or
      // terminalInstance's pin,
      //
      // It always reallocate all net's informations

      if(isTerminal == false) {
        io = 0;

        pin = &pinInstance[pid];
        // new(pin) PIN();
        curNet->pin[pinId++] = pin;

        curModule = &moduleInstance[moduleID];
        AddPinInfoForModuleAndTerminal(&curModule->pin, &curModule->pof,
                                        curModule->pinCNTinObject++, offset,
                                        moduleID, idx, j+1, pid++, io, isTerminal);
      }
      else {
        // IO floating placement: skip this
        // curTerminal = &terminalInstance[moduleID];
        // AddPinInfoForModuleAndTerminal(&curTerminal->pin, &curTerminal->pof,
        //                                 curTerminal->pinCNTinObject++, offset,
        //                                 moduleID, idx, j+1, pid++, io, isTerminal);
      }
    }
  }
  assert(netCur == netCNT);
  printf("INFO:  #NET=%d\n", netCNT);
  printf("INFO:  #PIN=%d\n", pinCNT);
  printf("INFO:    Maximum Net Degree is %d\n", max_net_deg);


  if (!fIncre && !fFast)
  {
    double aspectRatio = 1.0;
    double whiteSpace = 30;
    unsigned numCoreCells=Abc_NtkNodeNum(pNtkTemp)+Abc_NtkLatchNum(pNtkTemp);
    double targetLayoutArea = coreCellArea/(1.0-(whiteSpace/100.0));
    unsigned numCoreRows=(aspectRatio>0.0) ? (Abc_Rint(sqrt(targetLayoutArea/aspectRatio)/coreHeight)) : 0;
    numTerms=Abc_NtkPiNum(pNtkTemp)+Abc_NtkPoNum(pNtkTemp);
    unsigned totalWidth=coreCellArea/coreHeight;
    double layoutHeight = numCoreRows * coreHeight;
    double layoutWidth = Abc_Rint(targetLayoutArea/layoutHeight) + 1;
    double actualLayoutArea = layoutWidth * layoutHeight;

    // extract scl #################################################
    int origin_y=-floor(numCoreRows / 2);
    int origin_x = -floor(layoutWidth / 2);
    char * rowOrients[2] = {"N", "FS"};
    char symmetry='Y';
    double sitewidth=1.0;
    double spacing=1.0;
        
    unsigned rowId;

    row_cnt = numCoreRows;
    row_st = (ROW *)malloc(sizeof(struct ROW) * row_cnt);
    // std::vector<ROW> row_st(row_cnt);

    for( rowId=0 ; rowId<numCoreRows ; rowId++, origin_y += coreHeight )
    {
      ROW *row = &row_st[rowId];
      new(row) ROW();

      row->pmin.y = origin_y;  // Coordinate
      row->size.y = coreHeight;  // Height
      row->pmax.y = row->pmin.y + row->size.y;
      row->site_wid = (unsigned)sitewidth;  // Sitewidth
      row->site_spa = (unsigned)spacing;  // Sitespacing
      row->ori = rowOrients[rowId%2];  // Siteorient
      row->isYSymmetry = true;
      row->pmin.x = origin_x;  // SubrowOrigin
      row->x_cnt = (unsigned)layoutWidth;  // NumSites

      row->size.x = row->x_cnt * row->site_spa;
      row->pmax.x = row->pmin.x + row->size.x;

      if(rowId == 0) {
        grow_pmin.Set(row->pmin);
        grow_pmax.Set(row->pmax);
      }
      else {
        grow_pmin.x = min(grow_pmin.x, (prec)row->pmin.x);
        grow_pmin.y = min(grow_pmin.y, (prec)row->pmin.y);
        grow_pmax.x = max(grow_pmax.x, (prec)row->pmax.x);
        grow_pmax.y = max(grow_pmax.y, (prec)row->pmax.y);
      }

      if(rowId == 0) {
        rowHeight = row->size.y;
        SITE_SPA = row->site_spa;
      }
      else if(rowHeight != row->size.y) {
        printf("Error: ROW HEIGHT INCONSISTENT!\n");
        exit(0);
      }
      else if(SITE_SPA != row->site_spa) {
        printf("Error: SITE SPACING INCONSISTENT!\n");
        exit(0);
      }
    }


  // //extract pl ********************** not used in IO floating placement!
  //   Vec_Ptr_t * vTerms = Vec_PtrAlloc ( numTerms ); 
  //   Vec_Ptr_t * vOrderedTerms = Vec_PtrAlloc ( numTerms ); 
  //   double layoutPerim = 2*layoutWidth + 2*layoutHeight;
  //   double nextLoc_x, nextLoc_y;
  //   double delta;
  //   unsigned termsOnTop, termsOnBottom, termsOnLeft, termsOnRight;
  //   unsigned t;
  //   int j;
  //   bool isFirst = true;

  //   termsOnTop = termsOnBottom = (unsigned)(Abc_Rint(numTerms*(layoutWidth/layoutPerim)));
  //   termsOnLeft = numTerms - (termsOnTop+termsOnBottom);
  //   termsOnRight = (unsigned)(ceil(termsOnLeft/2.0));
  //   termsOnLeft -= termsOnRight;

  //   Abc_NtkForEachPi( pNtkTemp, pTerm, i )
  //   Vec_PtrPush( vTerms, pTerm );    
  //   Abc_NtkForEachPo( pNtkTemp, pTerm, i )
  //   Vec_PtrPush( vTerms, pTerm );
  //   // Ordering Pads
  //   vOrderedTerms=Io_NtkOrderingPads( pNtkTemp, vTerms );
  //   assert( termsOnTop+termsOnBottom+termsOnLeft+termsOnRight == (unsigned)Vec_PtrSize(vOrderedTerms) );

  //   int flag = 1;
  //   // terminal case 1
  //   nextLoc_x = -floor(layoutWidth / 2);
  //   nextLoc_y = ceil(layoutHeight / 2);
  //   delta   = layoutWidth / termsOnTop;
  //   for(t = 0; t < termsOnTop; t++)
  //   {
  //     pTerm = (Abc_Obj_t *)Vec_PtrEntry( vOrderedTerms, t );
  //     if( Abc_ObjIsPi(pTerm) )
  //         sprintf( nodeName, "i%s_input", Abc_ObjName(Abc_ObjFanout0(pTerm)) ); 
  //     else
  //         sprintf( nodeName, "o%s_output", Abc_ObjName(pTerm) ); 
  //     // if( t && Abc_Rint(nextLoc_x) < Abc_Rint(nextLoc_x-delta)+termWidth )
  //     //     nextLoc_x++;  

  //     bool isTerminal = false;
  //     auto nodesMapPtr = nodesMap.find(string(nodeName));
  //     if(nodesMapPtr == nodesMap.end()) {
  //       runtimeError(
  //           string(string(".pl files' nodename is mismatched! : ") + string(nodeName))
  //               .c_str());
  //     }
  //     else {
  //       isTerminal = nodesMapPtr->second.isTerminal;
  //       moduleID = nodesMapPtr->second.index;
  //       // cout << "currentname: " << name << ", index: " <<
  //       // nodesMapPtr->second.index << endl;
  //     }

  //     assert(isTerminal);

  //     curTerminal = &terminalInstance[moduleID];
  //     curTerminal->pmin.x = (int)Abc_Rint(nextLoc_x);
  //     curTerminal->pmin.y = (int)Abc_Rint(nextLoc_y);

  //     curTerminal->center.x = curTerminal->pmin.x + 0.5 * curTerminal->size.x;
  //     curTerminal->center.y = curTerminal->pmin.y + 0.5 * curTerminal->size.y;

  //     curTerminal->pmax.x = curTerminal->pmin.x + curTerminal->size.x;
  //     curTerminal->pmax.y = curTerminal->pmin.y + curTerminal->size.y;

  //     if(isFirst) {
  //       terminal_pmin = curTerminal->pmin;
  //       terminal_pmax = curTerminal->pmax;

  //       isFirst = false;
  //     }
  //     else {
  //       terminal_pmin.x = min(terminal_pmin.x, curTerminal->pmin.x);
  //       terminal_pmin.y = min(terminal_pmin.y, curTerminal->pmin.y);

  //       terminal_pmax.x = max(terminal_pmax.x, curTerminal->pmax.x);
  //       terminal_pmax.y = max(terminal_pmax.y, curTerminal->pmax.y);
  //     }
  //     // nextLoc_x += delta;
  //     if ((nextLoc_x == ceil(layoutWidth / 2)) || (nextLoc_x == -floor(layoutWidth / 2) - 1))
  //     {
  //         nextLoc_y += 1;
  //         flag = -flag;
  //     }
  //     nextLoc_x += flag;
  //   }

  //   // terminal case 2
  //   // nextLoc_x = floor(.0);
  //   // nextLoc_y = floor(.0 - 2*coreHeight - termHeight);
  //   flag = 1;
  //   nextLoc_x = -floor(layoutWidth / 2);
  //   nextLoc_y = -floor(layoutHeight / 2) - 1;
  //   delta   = layoutWidth / termsOnBottom;
  //   for(;t < termsOnTop+termsOnBottom; t++)
  //   {
  //     pTerm = (Abc_Obj_t *)Vec_PtrEntry( vOrderedTerms, t );
  //     if( Abc_ObjIsPi(pTerm) )
  //         sprintf( nodeName, "i%s_input", Abc_ObjName(Abc_ObjFanout0(pTerm)) ); 
  //     else
  //         sprintf( nodeName, "o%s_output", Abc_ObjName(pTerm) ); 
  //     // if( t!=termsOnTop && Abc_Rint(nextLoc_x) < Abc_Rint(nextLoc_x-delta)+termWidth )
  //     //     nextLoc_x++;
  //     bool isTerminal = false;
  //     auto nodesMapPtr = nodesMap.find(string(nodeName));
  //     if(nodesMapPtr == nodesMap.end()) {
  //       runtimeError(
  //           string(string(".pl files' nodename is mismatched! : ") + string(nodeName))
  //               .c_str());
  //     }
  //     else {
  //       isTerminal = nodesMapPtr->second.isTerminal;
  //       moduleID = nodesMapPtr->second.index;
  //       // cout << "currentname: " << name << ", index: " <<
  //       // nodesMapPtr->second.index << endl;
  //     }

  //     assert(isTerminal);

  //     curTerminal = &terminalInstance[moduleID];
  //     curTerminal->pmin.x = (int)Abc_Rint(nextLoc_x);
  //     curTerminal->pmin.y = (int)Abc_Rint(nextLoc_y);

  //     curTerminal->center.x = curTerminal->pmin.x + 0.5 * curTerminal->size.x;
  //     curTerminal->center.y = curTerminal->pmin.y + 0.5 * curTerminal->size.y;

  //     curTerminal->pmax.x = curTerminal->pmin.x + curTerminal->size.x;
  //     curTerminal->pmax.y = curTerminal->pmin.y + curTerminal->size.y;

  //     if(isFirst) {
  //       terminal_pmin = curTerminal->pmin;
  //       terminal_pmax = curTerminal->pmax;

  //       isFirst = false;
  //     }
  //     else {
  //       terminal_pmin.x = min(terminal_pmin.x, curTerminal->pmin.x);
  //       terminal_pmin.y = min(terminal_pmin.y, curTerminal->pmin.y);

  //       terminal_pmax.x = max(terminal_pmax.x, curTerminal->pmax.x);
  //       terminal_pmax.y = max(terminal_pmax.y, curTerminal->pmax.y);
  //     }
  //   //  nextLoc_x     += delta;
  //   if ((nextLoc_x == ceil(layoutWidth / 2)) || (nextLoc_x == -floor(layoutWidth / 2) - 1))
  //   {
  //       nextLoc_y -= 1;
  //       flag = -flag;
  //   }
  //   nextLoc_x += flag;
  //   }

  //   //terminal case 3
  //   // nextLoc_x = floor(.0-2*coreHeight-termWidth);
  //   // nextLoc_y = floor(.0);
  //   flag = 1;
  //   nextLoc_x = -floor(layoutWidth / 2) - 1;
  //   nextLoc_y = -floor(layoutHeight / 2);
  //   delta   = layoutHeight / termsOnLeft;
  //   for(;t < termsOnTop+termsOnBottom+termsOnLeft; t++)
  //   {
  //     pTerm = (Abc_Obj_t *)Vec_PtrEntry( vOrderedTerms, t );
  //     if( Abc_ObjIsPi(pTerm) )
  //         sprintf( nodeName, "i%s_input", Abc_ObjName(Abc_ObjFanout0(pTerm)) ); 
  //     else
  //         sprintf( nodeName, "o%s_output", Abc_ObjName(pTerm) ); 
  //     // if( Abc_Rint(nextLoc_y) < Abc_Rint(nextLoc_y-delta)+termHeight )
  //     //     nextLoc_y++;
  //     bool isTerminal = false;
  //     auto nodesMapPtr = nodesMap.find(string(nodeName));
  //     if(nodesMapPtr == nodesMap.end()) {
  //       runtimeError(
  //           string(string(".pl files' nodename is mismatched! : ") + string(nodeName))
  //               .c_str());
  //     }
  //     else {
  //       isTerminal = nodesMapPtr->second.isTerminal;
  //       moduleID = nodesMapPtr->second.index;
  //       // cout << "currentname: " << name << ", index: " <<
  //       // nodesMapPtr->second.index << endl;
  //     }

  //     assert(isTerminal);

  //     curTerminal = &terminalInstance[moduleID];
  //     curTerminal->pmin.x = (int)Abc_Rint(nextLoc_x);
  //     curTerminal->pmin.y = (int)Abc_Rint(nextLoc_y);

  //     curTerminal->center.x = curTerminal->pmin.x + 0.5 * curTerminal->size.x;
  //     curTerminal->center.y = curTerminal->pmin.y + 0.5 * curTerminal->size.y;

  //     curTerminal->pmax.x = curTerminal->pmin.x + curTerminal->size.x;
  //     curTerminal->pmax.y = curTerminal->pmin.y + curTerminal->size.y;

  //     if(isFirst) {
  //       terminal_pmin = curTerminal->pmin;
  //       terminal_pmax = curTerminal->pmax;

  //       isFirst = false;
  //     }
  //     else {
  //       terminal_pmin.x = min(terminal_pmin.x, curTerminal->pmin.x);
  //       terminal_pmin.y = min(terminal_pmin.y, curTerminal->pmin.y);

  //       terminal_pmax.x = max(terminal_pmax.x, curTerminal->pmax.x);
  //       terminal_pmax.y = max(terminal_pmax.y, curTerminal->pmax.y);
  //     }
  //     // nextLoc_y     += delta;
  //     if ((nextLoc_y == ceil(layoutHeight / 2)) || (nextLoc_y == -floor(layoutHeight / 2) - 1))
  //     {
  //         nextLoc_x -= 1;
  //         flag = -flag;
  //     }
  //     nextLoc_y += flag;
  //   }

  //   //terminal case 4
  //   // nextLoc_x = ceil(layoutWidth+2*coreHeight);
  //   // nextLoc_y = floor(.0);
  //   flag = 1;
  //   nextLoc_x = ceil(layoutWidth / 2);
  //   nextLoc_y = -floor(layoutHeight / 2);
  //   delta   = layoutHeight / termsOnRight;
  //   for(;t < termsOnTop+termsOnBottom+termsOnLeft+termsOnRight; t++)
  //   {
  //     pTerm = (Abc_Obj_t *)Vec_PtrEntry( vOrderedTerms, t );
  //     if( Abc_ObjIsPi(pTerm) )
  //         sprintf( nodeName, "i%s_input", Abc_ObjName(Abc_ObjFanout0(pTerm)) ); 
  //     else
  //         sprintf( nodeName, "o%s_output", Abc_ObjName(pTerm) ); 
  //     // if( Abc_Rint(nextLoc_y) < Abc_Rint(nextLoc_y-delta)+termHeight )
  //     //     nextLoc_y++;

  //     bool isTerminal = false;
  //     auto nodesMapPtr = nodesMap.find(string(nodeName));
  //     if(nodesMapPtr == nodesMap.end()) {
  //       runtimeError(
  //           string(string(".pl files' nodename is mismatched! : ") + string(nodeName))
  //               .c_str());
  //     }
  //     else {
  //       isTerminal = nodesMapPtr->second.isTerminal;
  //       moduleID = nodesMapPtr->second.index;
  //       // cout << "currentname: " << name << ", index: " <<
  //       // nodesMapPtr->second.index << endl;
  //     }

  //     assert(isTerminal);

  //     curTerminal = &terminalInstance[moduleID];
  //     curTerminal->pmin.x = (int)Abc_Rint(nextLoc_x);
  //     curTerminal->pmin.y = (int)Abc_Rint(nextLoc_y);

  //     curTerminal->center.x = curTerminal->pmin.x + 0.5 * curTerminal->size.x;
  //     curTerminal->center.y = curTerminal->pmin.y + 0.5 * curTerminal->size.y;

  //     curTerminal->pmax.x = curTerminal->pmin.x + curTerminal->size.x;
  //     curTerminal->pmax.y = curTerminal->pmin.y + curTerminal->size.y;

  //     if(isFirst) {
  //       terminal_pmin = curTerminal->pmin;
  //       terminal_pmax = curTerminal->pmax;

  //       isFirst = false;
  //     }
  //     else {
  //       terminal_pmin.x = min(terminal_pmin.x, curTerminal->pmin.x);
  //       terminal_pmin.y = min(terminal_pmin.y, curTerminal->pmin.y);

  //       terminal_pmax.x = max(terminal_pmax.x, curTerminal->pmax.x);
  //       terminal_pmax.y = max(terminal_pmax.y, curTerminal->pmax.y);
  //     }
  //     // nextLoc_y     += delta;
  //     if ((nextLoc_y == ceil(layoutHeight / 2)) || (nextLoc_y == -floor(layoutHeight / 2) - 1))
  //     {
  //         nextLoc_x += 1;
  //         flag = -flag;
  //     }
  //     nextLoc_y += flag;
  //     // printf("%s, %d, %d, %.2f, %.2f && ", nodeName, pTerm->Id, moduleID, curTerminal->center.x, curTerminal->center.y);
  //   }
  //   Vec_PtrFree(vTerms);
  //   Vec_PtrFree(vOrderedTerms);
  }

  //internal latches
  if( !Abc_NtkIsComb(pNtkTemp) )
  {
    Abc_NtkForEachLatch( pNtkTemp, pLatch, i )
    {
      Abc_Obj_t * pNetLi, * pNetLo;
    
      pNetLi = Abc_ObjFanin0( Abc_ObjFanin0(pLatch) );
      pNetLo = Abc_ObjFanout0( Abc_ObjFanout0(pLatch) );
      /// write the latch line
      sprintf( nodeName, "%s_%s_latch", Abc_ObjName(pNetLi), Abc_ObjName(pNetLo) );

      bool isTerminal = false;
      auto nodesMapPtr = nodesMap.find(string(nodeName));
      if(nodesMapPtr == nodesMap.end()) {
        runtimeError(
            string(string(".pl files' nodename is mismatched! : ") + string(nodeName))
                .c_str());
      }
      else {
        isTerminal = nodesMapPtr->second.isTerminal;
        moduleID = nodesMapPtr->second.index;
        // cout << "currentname: " << name << ", index: " <<
        // nodesMapPtr->second.index << endl;
      }

      assert(!isTerminal);

      curModule = &moduleInstance[moduleID];
      if (fIncre)
      {
        curModule->pmin.x = pLatch->xPos - curModule->half_size.x;
        curModule->pmin.y = pLatch->yPos - curModule->half_size.y;
      }
      else
      {
        curModule->pmin.x = 0;
        curModule->pmin.y = 0;
      }

      curModule->center.x = curModule->pmin.x + curModule->half_size.x;
      curModule->center.y = curModule->pmin.y + curModule->half_size.y;

      curModule->pmax.x = curModule->pmin.x + curModule->size.x;
      curModule->pmax.y = curModule->pmin.y + curModule->size.y;
    }
  }

  //internal nodes
  Abc_NtkForEachNode( pNtkTemp, pNode, i )
  {
    // unsigned sizex=0, sizey=coreHeight, isize=0;
    //double nx, ny, xstep, ystep;    
    Abc_Obj_t * pNeti, *pNeto;
    int j;
      
    memset(nodeName, 0, sizeof(nodeName));

    // write the network after mapping 
    if ( Abc_NtkHasMapping(pNode->pNtk) )
    {
      Mio_Gate_t * pGate = (Mio_Gate_t *)pNode->pData;
      Mio_Pin_t * pGatePin;
      // write the node gate
      for ( pGatePin = Mio_GateReadPins(pGate), j = 0; pGatePin; pGatePin = Mio_PinReadNext(pGatePin), j++ )
          sprintf( nodeName + strlen(nodeName), "%s_", Abc_ObjName( Abc_ObjFanin(pNode,j) ) );
      assert ( j == Abc_ObjFaninNum(pNode) );
      sprintf( nodeName + strlen(nodeName), "%s_%s", Abc_ObjName( Abc_ObjFanout0(pNode) ), Mio_GateReadName(pGate) );
    }
    else
    {
      Abc_ObjForEachFanin( pNode, pNeti, j )
          sprintf( nodeName + strlen(nodeName), "%s_", Abc_ObjName(pNeti) );
      Abc_ObjForEachFanout( pNode, pNeto, j )
          sprintf( nodeName + strlen(nodeName), "%s_", Abc_ObjName(pNeto) );    
      sprintf( nodeName + strlen(nodeName), "name" );
    }

    bool isTerminal = false;
    auto nodesMapPtr = nodesMap.find(string(nodeName));
    if(nodesMapPtr == nodesMap.end()) {
      runtimeError(
          string(string(".pl files' nodename is mismatched! : ") + string(nodeName))
              .c_str());
    }
    else {
      isTerminal = nodesMapPtr->second.isTerminal;
      moduleID = nodesMapPtr->second.index;
      // cout << "currentname: " << name << ", index: " <<
      // nodesMapPtr->second.index << endl;
    }

    assert(!isTerminal);

    curModule = &moduleInstance[moduleID];
    if (fIncre)
    {
      curModule->pmin.x = pNode->xPos - curModule->half_size.x;
      curModule->pmin.y = pNode->yPos - curModule->half_size.y;
    }
    else
    {
      curModule->pmin.x = 0;
      curModule->pmin.y = 0;
    }

    curModule->center.x = curModule->pmin.x + curModule->half_size.x;
    curModule->center.y = curModule->pmin.y + curModule->half_size.y;

    curModule->pmax.x = curModule->pmin.x + curModule->size.x;
    curModule->pmax.y = curModule->pmin.y + curModule->size.y;
  }
  // post reading
  FPOS tier_min, tier_max;
  int tier_row_cnt = 0;

  get_mms_3d_dim(&tier_min, &tier_max, &tier_row_cnt);
  transform_3d(&tier_min, &tier_max, tier_row_cnt);
  post_read_3d();

  Abc_NtkDelete( pNtkTemp );
}

void MapToAbcNtk(Abc_Ntk_t *pNtk, int fFast){
  // int i, k = 0;
  // Abc_Obj_t *pNode;
  // Abc_NtkForEachNode(pNtk, pNode, i)
  // {
  //   pNode->xPos = moduleInstance[k].pmin.x;
  //   pNode->yPos = moduleInstance[k++].pmin.y;
  // }
  // assert (k == moduleCNT);
  int i;
  Abc_Obj_t *pLatch, *pNode, *pi, *po;
  MODULE moduleTmp;
  TIER *tier = &tier_st[0];
  float scaleX, scaleY;

  // position info
  Abc_NtkForEachLatch(pNtk, pLatch, i)
  {
    moduleTmp = moduleInstance[abcNodesMap[pLatch->pCopy]];
    pLatch->xPos = Abc_ObjFanin0(pLatch)->xPos = Abc_ObjFanout0(pLatch)->xPos = moduleTmp.center.x;
    pLatch->yPos = Abc_ObjFanin0(pLatch)->yPos = Abc_ObjFanout0(pLatch)->yPos = moduleTmp.center.y;
    pLatch->rawPos = Abc_ObjFanin0(pLatch)->rawPos = Abc_ObjFanout0(pLatch)->rawPos = 0;
    // SQRT2 = smoothing parameter for density_size calculation
    if(moduleTmp.size.x < tier->bin_stp.x * SQRT2) {
      scaleX = moduleTmp.size.x / (tier->bin_stp.x * SQRT2);
      pLatch->half_den_sizeX = tier->half_bin_stp.x * SQRT2;
    }
    else {
      scaleX = 1.0;
      pLatch->half_den_sizeX = moduleTmp.size.x / 2;
    }

    if(moduleTmp.size.y < tier->bin_stp.y * SQRT2) {
      scaleY = moduleTmp.size.y / (tier->bin_stp.y * SQRT2);
      pLatch->half_den_sizeY = tier->half_bin_stp.y * SQRT2;
    }
    else {
      scaleY = 1.0;
      pLatch->half_den_sizeY = moduleTmp.size.y / 2;
    }
    pLatch->den_scal = scaleX * scaleY;
  }

  Abc_NtkForEachNode(pNtk, pNode, i)
  {
    moduleTmp = moduleInstance[abcNodesMap[pNode->pCopy]];
    pNode->xPos = moduleTmp.center.x;
    pNode->yPos = moduleTmp.center.y;
    // printf("%d, %f, %f\n", pNode->Id, pNode->xPos, pNode->yPos);
    pNode->rawPos = 0;
    // SQRT2 = smoothing parameter for density_size calculation
    if(moduleTmp.size.x < tier->bin_stp.x * SQRT2) {
      scaleX = moduleTmp.size.x / (tier->bin_stp.x * SQRT2);
      pNode->half_den_sizeX = tier->half_bin_stp.x * SQRT2;
    }
    else {
      scaleX = 1.0;
      pNode->half_den_sizeX = moduleTmp.size.x / 2;
    }

    if(moduleTmp.size.y < tier->bin_stp.y * SQRT2) {
      scaleY = moduleTmp.size.y / (tier->bin_stp.y * SQRT2);
      pNode->half_den_sizeY = tier->half_bin_stp.y * SQRT2;
    }
    else {
      scaleY = 1.0;
      pNode->half_den_sizeY = moduleTmp.size.y / 2;
    }
    pNode->den_scal = scaleX * scaleY;
  }

  // // IO placement results ******************** not used in IO floating mode!
  // int idx = 0;
  // if (!fFast)
  // {
  //   Abc_NtkForEachPi(pNtk, pi, i)
  //   {
  //     assert(abcTermsMap[pi->pCopy] == idx);
  //     pi->xPos = terminalInstance[abcTermsMap[pi->pCopy]].center.x;
  //     pi->yPos = terminalInstance[abcTermsMap[pi->pCopy]].center.y;
  //     pi->rawPos = 0;
  //     idx++;
  //   }
  //   Abc_NtkForEachPo(pNtk, po, i)
  //   {
  //     assert(abcTermsMap[po->pCopy] == idx);
  //     po->xPos = terminalInstance[abcTermsMap[po->pCopy]].center.x;
  //     po->yPos = terminalInstance[abcTermsMap[po->pCopy]].center.y;
  //     po->rawPos = 0;
  //     idx++;
  //     // printf("%d, %d, %.2f, %.2f ** ", po->Id, abcTermsMap[po->pCopy], po->xPos, po->yPos);
  //   }
  // }
  // else
  // {
  //   Abc_NtkForEachPi(pNtk, pi, i)
  //   {
  //     pi->xPos = terminalInstance[idx].center.x;
  //     pi->yPos = terminalInstance[idx].center.y;
  //     pi->rawPos = 0;
  //     idx++;
  //   }
  //   Abc_NtkForEachPo(pNtk, po, i)
  //   {
  //     po->xPos = terminalInstance[idx].center.x;
  //     po->yPos = terminalInstance[idx].center.y;
  //     po->rawPos = 0;
  //     idx++;
  //     // printf("%d, %d, %.2f, %.2f ** ", po->Id, abcTermsMap[po->pCopy], po->xPos, po->yPos);
  //   }
  // }

  // density info
  if (!pNtk->binsDensity)
  {
    printf("############ Create %d bins for abcNtk! #############\n", tier->tot_bin_cnt);
    pNtk->binsDensity = (float *)malloc(sizeof(float) * tier->tot_bin_cnt);
  }
  else if (pNtk->nBins != tier->tot_bin_cnt)
  {
    printf("############ binNumber changed from %d to %d! #############\n", pNtk->nBins, tier->tot_bin_cnt);
    free(pNtk->binsDensity);
    pNtk->binsDensity = (float *)malloc(sizeof(float) * tier->tot_bin_cnt);
  }
  pNtk->nBins = tier->tot_bin_cnt;
  pNtk->binModuleRatio = tier->bin_area / total_modu_area;
  pNtk->binOriX = tier->bin_org.x;
  pNtk->binOriY = tier->bin_org.y;
  pNtk->binStepX = tier->bin_stp.x;
  pNtk->binStepY = tier->bin_stp.y;
  pNtk->binDimX = tier->dim_bin.x;
  pNtk->binDimY = tier->dim_bin.y;
  pNtk->halfBinRatio = 0.5 / sqrt(moduleCNT * pNtk->binModuleRatio);
  pNtk->invBinArea = tier->inv_bin_area;
  for (int i = 0; i < tier->tot_bin_cnt; i++) {
    BIN *bp = &tier->bin_mat[i];
    pNtk->binsDensity[i] = bp->den2;
    // std::cout << i << ' ' << pNtk->binsDensity[i] << std::endl;
  }
  printf("halfBinRatio = %f\n", pNtk->halfBinRatio);
  pair<double, double> hpwl = GetUnscaledHpwl();
  pNtk->globalHpwl = hpwl.first + hpwl.second;
  printf("globalHpwl = %f\n", pNtk->globalHpwl);

  // // global overflow computation
  // float overflow = 0;
  // for (int i = 0; i < pNtk->nBins; ++i)
  // {
  //     // printf("%.2f && ", pNtk->binsDensity[i]);
  //     overflow += (pNtk->binsDensity[i] > 1) ? pNtk->binsDensity[i] - 1 : 0;
  // }
  // overflow *= pNtk->binModuleRatio;
  // printf("Overflow1 = %f\n", overflow);

  return;
}


// mgwoo
void init() {
  char str_lg[BUF_SZ] = {
      0,
  };
  char str_dp[BUF_SZ] = {
      0,
  };
  char str_dp2[BUF_SZ] = {
      0,
  };
  char str_dp3[BUF_SZ] = {
      0,
  };
  char result_file[BUF_SZ] = {
      0,
  };
  int ver_num = 0;

  inv_RAND_MAX = (prec)1.0 / RAND_MAX;

  sprintf(global_router, "NCTUgr.ICCAD2012");

  switch(detailPlacer) {
    case FastPlace:
      sprintf(str_lg, "%s", "_eplace_lg");
      sprintf(str_dp, "%s", "_FP_dp");
      break;

    case NTUplace3:
      sprintf(str_lg, "%s", "_eplace_lg");
      sprintf(str_dp, "%s", ".eplace-gp.ntup");
      sprintf(str_dp2, "%s", ".eplace-cGP3D.ntup");
      break;

    case NTUplace4h:
      sprintf(str_lg, "%s", "_eplace_lg");
      sprintf(str_dp, "%s", ".eplace-gp.ntup");
      sprintf(str_dp2, "%s", ".eplace-cGP3D.ntup");
      break;
  }

  sprintf(str_dp3, ".%s.%s", bmFlagCMD.c_str(), "eplace");

  INPUT_FLG = ETC;

  global_macro_area_scale = target_cell_den;
  PrintInfoPrec("TargetDensity", target_cell_den, 0);

  wcof_flg = /* 1 */ 2 /* 3 */;

  // see ePlace-MS 
  // 8 * binSize.
  //
  // see: wlen.cpp: wcof_init function also
  //
  switch(WLEN_MODEL) {
    case LSE:
      // 10 !!!
      wcof00.x = wcof00.y = 0.1;
      break;

    case WA:
      if(INPUT_FLG == ISPD05 || INPUT_FLG == ISPD06 || INPUT_FLG == ISPD ||
         INPUT_FLG == MMS || INPUT_FLG == SB || INPUT_FLG == ETC) {

        // 8 !!!
        wcof00.x = wcof00.y = 0.125;
      }
      else if(INPUT_FLG == IBM) {
        // 2 !!!
        wcof00.x = wcof00.y = 0.50;
      }
      break;
  }

  SetMAX_EXP_wlen();

  // benchMark directory settings
  getcwd(currentDir, BUF_SZ);
  sourceCodeDir = getexepath();

  //
  //
  // benchName(== gbch) : benchmark name
  // gbch_dir : benchmark directory
  //
  //

  string fileCMD = (auxCMD != "") ? auxCMD : defName;

  //
  // check '/' from back side
  //
  int slashPos = fileCMD.rfind('/');

  //
  // extract benchName ~~~~/XXXX.aux
  //                        <------>
  benchName =
      (slashPos == -1)
          ? fileCMD
          : fileCMD.substr(slashPos + 1, fileCMD.length() - slashPos - 1);
  sprintf(gbch_dir, "%s",
          (slashPos == -1) ? currentDir : fileCMD.substr(0, slashPos).c_str());

  // if benchName has [.aux] extension
  // remove [.aux]
  int dotPos = benchName.rfind(".");
  if(benchName.length() - dotPos == 4) {
    benchName = benchName.substr(0, dotPos);
  }

  sprintf(gbch, "%s", benchName.c_str());
  sprintf(output_dir, "%s/%s/%s", outputCMD.c_str(), bmFlagCMD.c_str(),
          benchName.c_str());

  // generate folder if output folders not exist.
  struct stat fileStat;
  if(stat(output_dir, &fileStat) < 0) {
    string mkdir_cmd = "mkdir -p " + string(output_dir);
    system(mkdir_cmd.c_str());
  }

  // experiment folder could be given by user.
  // original settings
  if(experimentCMD == "") {
    // 'experimentXX' -1 checker.
    for(ver_num = 0;; ver_num++) {
      sprintf(dir_bnd, "%s/experiment%03d", output_dir, ver_num);

      // until not exists.
      struct stat fileStat;
      if(stat(dir_bnd, &fileStat) < 0) {
        break;
      }
    }
  }
  // follow user settings
  else {
    sprintf(dir_bnd, "%s/%s", output_dir, experimentCMD.c_str());
  }

  // output 'experimentXX' directory generator.
  string mkdir_cmd = "mkdir -p " + string(dir_bnd);
  system(mkdir_cmd.c_str());

  // PL file writer info update
  sprintf(gIP_pl, "%s/%s.eplace-ip.pl", dir_bnd, gbch);
  sprintf(gGP_pl, "%s/%s.eplace-gp.pl", dir_bnd, gbch);
  sprintf(gmGP2D_pl, "%s/%s.eplace-mGP2D.pl", dir_bnd, gbch);
  sprintf(gGP3_pl, "%s/%s.eplace-cGP2D.pl", dir_bnd, gbch);
  sprintf(gGP_pl_file, "%s.eplace-gp.pl", gbch);
  sprintf(gLG_pl, "%s/%s%s.pl", dir_bnd, gbch, str_lg);
  sprintf(gDP_pl, "%s/%s%s.pl", dir_bnd, gbch, str_dp);
  sprintf(gDP2_pl, "%s/%s%s.pl", dir_bnd, gbch, str_dp2);
  sprintf(gDP3_pl, "%s/%s%s.pl", dir_bnd, gbch, str_dp3);
  sprintf(result_file, "%s/res", dir_bnd);

  sprintf(defGpOutput, "%s/%s_gp.def", dir_bnd, gbch);
  sprintf(defOutput, "%s/%s_final.def", dir_bnd, gbch);

  PrintInfoInt("ExperimentIndex", ver_num);
  PrintInfoString("DirectoryPath", dir_bnd);

  sprintf(bench_aux, "%s/%s.aux", gbch_dir, gbch);
  sprintf(gbch_aux, "%s.aux", gbch);

  strcpy(gDP_log, gbch);
  strcat(gDP_log, "_DP.log");

  // mgwoo
  // mkdirPlot();
}

void calcTSVweight() {
  TSV_WEIGHT = TSV_CAP * ((prec)numLayer) /
               (ROW_CAP * (prec)(tier_st[0].row_cnt)) * gtsv_cof;
  if(INPUT_FLG == IBM)
    TSV_WEIGHT = 0.73 * gtsv_cof;
  printf("INFO:  TSV_WEIGHT = %.6E\n", TSV_WEIGHT);
}

void initialPlacement_main() {
  STAGE = INITIAL_PLACE;
  initial_placement();
  UpdateNetAndGetHpwl();

  PrintUnscaledHpwl("Initial Placement");
  place_backup = place;
}

void tmGP3DglobalPlacement_main() {
  STAGE = mGP3D;
  // ALPHAmGP = initialALPHA;
  gp_opt();
  UpdateNetAndGetHpwl();
  printf("RESULT:\n");
  printf("   HPWL(tGP3D): %.4f (%.4f, %.4f)\n\n",
         total_hpwl.x + total_hpwl.y, total_hpwl.x, total_hpwl.y);
  fflush(stdout);
}

void tmGP2DglobalPlacement_main() {
  STAGE = mGP2D;
  // ALPHA = initialALPHA;
  tier_assign(MIXED);
  calc_tier_WS();
  setup_before_opt_mGP2D();
  gp_opt();
  post_mGP2D_delete();
  fflush(stdout);
  UpdateNetAndGetHpwl();
  printf("RESULT:\n");
  printf("   HPWL(tGP2D): %.4f (%.4f, %.4f)\n\n", total_hpwl.x + total_hpwl.y,
         total_hpwl.x, total_hpwl.y);
  fflush(stdout);
}

void mGP3DglobalPlacement_main() {
  STAGE = mGP3D;
  // ALPHA = initialALPHA;
  gp_opt();
  isFirst_gp_opt = false;
  UpdateNetAndGetHpwl();
  if(dynamicStepCMD) {
    reassign_trial_2ndOrder_lastEP(total_hpwl.x + total_hpwl.y);
  }
  printf("RESULT:\n");
  printf("   HPWL(mGP3D): %.4f (%.4f, %.4f)\n\n",
         total_hpwl.x + total_hpwl.y, total_hpwl.x, total_hpwl.y);
  fflush(stdout);
}

void mGP2DglobalPlacement_main() {
  STAGE = mGP2D;
  // ALPHA = initialALPHA;
  tier_assign(MIXED);
  calc_tier_WS();
  setup_before_opt_mGP2D();
  gp_opt();
  isFirst_gp_opt = false;
  /*if (INPUT_FLG != ISPD)*/ post_mGP2D_delete();
  fflush(stdout);
  UpdateNetAndGetHpwl();
  if(dynamicStepCMD) {
    reassign_trial_2ndOrder_lastEP(total_hpwl.x + total_hpwl.y);
  }
  auto hpwl = GetUnscaledHpwl(); 
  PrintInfoPrec("Nesterov: HPWL", hpwl.first + hpwl.second, 1);
}

void tcGP3DglobalPlacement_main() {
  STAGE = cGP3D;
  // ALPHA = initialALPHA;
  UpdateGcellCoordiFromModule();  // cell_macro_copy ();
  gp_opt();
  UpdateNetAndGetHpwl();
  printf("RESULT:\n");
  printf("   HPWL(tGP3D): %.4f (%.4f, %.4f)\n\n",
         total_hpwl.x + total_hpwl.y, total_hpwl.x, total_hpwl.y);
  fflush(stdout);
}

void cGP3DglobalPlacement_main() {
  STAGE = cGP3D;
  // ALPHA = initialALPHA;
  UpdateGcellCoordiFromModule();  // cell_macro_copy ();
  gp_opt();
  isFirst_gp_opt = false;
  UpdateNetAndGetHpwl();
  if(dynamicStepCMD) {
    reassign_trial_2ndOrder_lastEP(total_hpwl.x + total_hpwl.y);
  }
  printf("RESULT:\n");
  printf("   HPWL(cGP3D): %.4f (%.4f, %.4f)\n\n",
         total_hpwl.x + total_hpwl.y, total_hpwl.x, total_hpwl.y);
  fflush(stdout);
}

void tcGP2DglobalPlacement_main() {
  STAGE = cGP2D;
  // ALPHA = initialALPHA;
  tier_assign(STDCELLonly);
  setup_before_opt_cGP2D();
  gp_opt();
  UpdateNetAndGetHpwl();
  printf("RESULT:\n");
  printf("   HPWL(tGP2D): %.4f (%.4f, %.4f)\n\n", total_hpwl.x + total_hpwl.y,
         total_hpwl.x, total_hpwl.y);
  fflush(stdout);
}

void cGP2DglobalPlacement_main() {
  STAGE = cGP2D;
  // ALPHA = initialALPHA;

  tier_assign(STDCELLonly);
  setup_before_opt_cGP2D();
  gp_opt();

  isFirst_gp_opt = false;
  UpdateNetAndGetHpwl();

  auto hpwl = GetUnscaledHpwl(); 
  PrintInfoPrec("Nesterov: HPWL", hpwl.first + hpwl.second, 1);
}

void macroLegalization_main() {
  STAGE = mLG3D;
  pre_mac_tier();
  sa_macro_lg();
  UpdateNetAndGetHpwl();
  if(dynamicStepCMD) {
    reassign_trial_2ndOrder_lastEP(total_hpwl.x + total_hpwl.y);
  }
  printf("RESULT:\n");
  printf("   HPWL(mLG): %.4f (%.4f, %.4f)\n\n", total_hpwl.x + total_hpwl.y,
         total_hpwl.x, total_hpwl.y);
  fflush(stdout);
  post_mac_tier();
}

void WriteBookshelfForGR() {
  PrintProcBegin("Write Bookshelf");
  // temporary update net->pin2 to write bookshelf
  update_pin2();

  char targetDir[BUF_SZ] = {0, };
  sprintf( targetDir, "%s/router_base/", dir_bnd);
  cout << targetDir << endl;

  char cmd[BUF_SZ] = {0, };
  sprintf( cmd, "mkdir -p %s", targetDir);
  system(cmd);

    // call Write Bookshelf function by its tier
    WriteBookshelfWithTier(
        targetDir, 
        // tier number
        0, 
        // *.shape support
//        (detailPlacer == NTUplace3 || shapeMap.size() == 0) ? false : true);
        true, true, true);
  PrintProcEnd("Write Bookshelf");
}

void WriteBookshelf() {
  printf("INFO:  WRITE BOOKSHELF...");
  // temporary update net->pin2 to write bookshelf
  update_pin2();
  
  char targetDir[BUF_SZ] = {0, };
  sprintf( targetDir, "%s/tiers/0", dir_bnd);
  cout << targetDir << endl;

  char cmd[BUF_SZ] = {0, };
  sprintf( cmd, "mkdir -p %s", targetDir);
  system(cmd);

    // call Write Bookshelf function by its tier
    WriteBookshelfWithTier(
        targetDir,
        // tier number
        0, 
        // *.shape support
        (detailPlacer == NTUplace3) ? false : true,
        false);
  printf("PROC:  END WRITE BOOKSHELF\n\n");
  fflush(stdout);
}

void free_trial_mallocs() {
  // free(moduleInstance);
  // free(terminalInstance);
  // free(netInstance);
  // free(pinInstance);
  free(gcell_st);
  free(row_st);
  free(tier_st);
  free(place_st);
}

void free_all() {
  // 1. Clean Modules
  if (moduleInstance) {
      for(int i=0; i<moduleCNT; i++) {
          if(moduleInstance[i].pin) free(moduleInstance[i].pin);
          if(moduleInstance[i].pof) free(moduleInstance[i].pof);
      }
      free(moduleInstance);
  }

  // 2. Clean Terminals
  if (terminalInstance) {
      for(int i=0; i<terminalCNT; i++) {
          if(terminalInstance[i].pin) free(terminalInstance[i].pin);
          if(terminalInstance[i].pof) free(terminalInstance[i].pof);
      }
      free(terminalInstance);
  }

  // 3. Net Instance cleaning
  // (Note: pins are freed in the loop in Replace_main, but pin2 needs checking)
  // It is safer to move that logic inside here for consistency
  if (netInstance) {
      for(int i=0; i<netCNT; i++) {
          if(netInstance[i].pin)  free(netInstance[i].pin);
          if(netInstance[i].pin2) free(netInstance[i].pin2);
      }
      free(netInstance);
  }
  
  if (pinInstance) free(pinInstance);

  // 4. Clean GCells (Placement Cells)
  if (gcell_st) {
      for(int i=0; i<gcell_cnt; i++) {
          // if(gcell_st[i].pin) free(gcell_st[i].pin);
          // if(gcell_st[i].pof) free(gcell_st[i].pof);
          // Check if pin_tmp/pof_tmp are still active
          // if(gcell_st[i].pin_tmp) free(gcell_st[i].pin_tmp);
          // if(gcell_st[i].pof_tmp) free(gcell_st[i].pof_tmp);
      }
      free(gcell_st);
  }

  if (row_st) free(row_st);

  // 5. Clean Tiers
  if (tier_st) {
      // Assuming single tier for 2D, loop if necessary
      if(tier_st[0].bin_mat) free(tier_st[0].bin_mat);
      if(tier_st[0].cell_st) free(tier_st[0].cell_st);
      free(tier_st);
  }

  if (place_st) free(place_st);
}
