/**CFile****************************************************************

  FileName    [abcRefactor.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Network and node package.]

  Synopsis    [Resynthesis based on collapsing and refactoring.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: abcRefactor.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "base/abc/abc.h"
#include "bool/dec/dec.h"
#include "bool/kit/kit.h"
#include "math.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

typedef struct Abc_ManRef_t_   Abc_ManRef_t;
struct Abc_ManRef_t_
{
    // user specified parameters
    int              nNodeSizeMax;      // the limit on the size of the supernode
    int              nConeSizeMax;      // the limit on the size of the containing cone
    int              fVerbose;          // the verbosity flag
    // internal data structures
    Vec_Ptr_t *      vVars;             // truth tables
    Vec_Ptr_t *      vFuncs;            // functions
    Vec_Int_t *      vMemory;           // memory
    Vec_Str_t *      vCube;             // temporary
    Vec_Int_t *      vForm;             // temporary
    Vec_Ptr_t *      vVisited;          // temporary
    Vec_Ptr_t *      vLeaves;           // temporary
    // node statistics
    int              nLastGain;
    int              nNodesConsidered;
    int              nNodesRefactored;
    int              nNodesGained;
    int              nNodesBeg;
    int              nNodesEnd;
    // runtime statistics
    abctime          timeCut;
    abctime          timeTru;
    abctime          timeDcs;
    abctime          timeSop;
    abctime          timeFact;
    abctime          timeEval;
    abctime          timeRes;
    abctime          timeNtk;
    abctime          timeTotal;
};

static inline float Abc_RefObjWireDelay( Abc_Obj_t * pFrom, Abc_Obj_t * pTo )
{
    float dx, dy;
    if ( Abc_ObjIsTerm(pFrom) || Abc_ObjIsTerm(pTo) )
        return 0.0f;
    dx = pFrom->xPos - pTo->xPos;
    dy = pFrom->yPos - pTo->yPos;
    return sqrtf( dx * dx + dy * dy );
}

static void Abc_NtkRefactorPrecomputeTiming( Abc_Ntk_t * pNtk )
{
    Abc_Obj_t * pObj, * pNode, * pFanin, * pPo;
    int i, k;
    int ArrivalMaxT = 0;
    float ArrivalMaxG = 0.0f;

    assert( Abc_NtkIsStrash(pNtk) );

    Abc_NtkForEachObj( pNtk, pObj, i )
    {
        pObj->arrivalT = 0;
        pObj->arrivalG = 0.0f;
        pObj->reqT = ABC_INFINITY;
        pObj->reqG = (float)ABC_INFINITY;
    }

    Abc_NtkForEachNode( pNtk, pNode, i )
    {
        int ArrivalT = 0;
        float ArrivalG = 0.0f;
        Abc_ObjForEachFanin( pNode, pFanin, k )
        {
            int CandidateT = pFanin->arrivalT + 1;
            float CandidateG = pFanin->arrivalG + Abc_RefObjWireDelay( pFanin, pNode );
            if ( ArrivalT < CandidateT )
                ArrivalT = CandidateT;
            if ( ArrivalG < CandidateG )
                ArrivalG = CandidateG;
        }
        pNode->arrivalT = ArrivalT;
        pNode->arrivalG = ArrivalG;
    }

    Abc_NtkForEachPo( pNtk, pPo, i )
    {
        pFanin = Abc_ObjFanin0(pPo);
        pPo->arrivalT = pFanin->arrivalT;
        pPo->arrivalG = pFanin->arrivalG;
        if ( ArrivalMaxT < pFanin->arrivalT )
            ArrivalMaxT = pFanin->arrivalT;
        if ( ArrivalMaxG < pFanin->arrivalG )
            ArrivalMaxG = pFanin->arrivalG;
    }
    pNtk->timingGScale = ( ArrivalMaxT > 0 && ArrivalMaxG > 1.0e-4f ) ? ArrivalMaxG / (float)ArrivalMaxT : 1.0f;

    Abc_NtkForEachPo( pNtk, pPo, i )
    {
        pFanin = Abc_ObjFanin0(pPo);
        pPo->reqT = ArrivalMaxT;
        pPo->reqG = ArrivalMaxG;
        if ( pFanin->reqT > ArrivalMaxT )
            pFanin->reqT = ArrivalMaxT;
        if ( pFanin->reqG > ArrivalMaxG )
            pFanin->reqG = ArrivalMaxG;
    }

    Abc_NtkForEachNodeReverse( pNtk, pNode, i )
    {
        if ( pNode->reqT == ABC_INFINITY && pNode->reqG == (float)ABC_INFINITY )
            continue;
        Abc_ObjForEachFanin( pNode, pFanin, k )
        {
            if ( pNode->reqT != ABC_INFINITY && pFanin->reqT > pNode->reqT - 1 )
                pFanin->reqT = pNode->reqT - 1;
            if ( pNode->reqG != (float)ABC_INFINITY )
            {
                float CandidateG = pNode->reqG - Abc_RefObjWireDelay( pFanin, pNode );
                if ( pFanin->reqG > CandidateG )
                    pFanin->reqG = CandidateG;
            }
        }
    }
}

static void Abc_RefCutCollectNodes_rec( Abc_Obj_t * pObj, Vec_Ptr_t * vNodes, Vec_Ptr_t * vSeen )
{
    Abc_Obj_t * pFanin;
    int i;

    pObj = Abc_ObjRegular(pObj);
    if ( Vec_PtrFind(vSeen, pObj) >= 0 )
        return;
    if ( !pObj->marked && !Abc_NodeIsTravIdCurrent(pObj) )
        return;
    Vec_PtrPush( vSeen, pObj );
    if ( Abc_ObjIsNode(pObj) )
        Abc_ObjForEachFanin( pObj, pFanin, i )
            Abc_RefCutCollectNodes_rec( pFanin, vNodes, vSeen );
    Vec_PtrPush( vNodes, pObj );
}

static void Abc_RefCutUpdateArrival( Abc_Obj_t * pObj )
{
    Abc_Obj_t * pFanin;
    int i;

    if ( Abc_ObjIsCi(pObj) || Abc_ObjFaninNum(pObj) == 0 )
    {
        pObj->arrivalT = 0;
        pObj->arrivalG = 0.0f;
        return;
    }
    pObj->arrivalT = 0;
    pObj->arrivalG = 0.0f;
    Abc_ObjForEachFanin( pObj, pFanin, i )
    {
        int ArrivalT = pFanin->arrivalT + 1;
        float ArrivalG = pFanin->arrivalG + Abc_RefObjWireDelay( pFanin, pObj );
        if ( pObj->arrivalT < ArrivalT )
            pObj->arrivalT = ArrivalT;
        if ( pObj->arrivalG < ArrivalG )
            pObj->arrivalG = ArrivalG;
    }
}

static void Abc_RefCutUpdateRequired( Abc_Obj_t * pObj )
{
    Abc_Obj_t * pFanout;
    int i, nFanouts;

    pObj->reqT = ABC_INFINITY;
    pObj->reqG = (float)ABC_INFINITY;
    nFanouts = Vec_IntSize( &pObj->vFanouts ) - (pObj->marked ? 1 : 0);
    for ( i = 0; i < nFanouts; ++i )
    {
        pFanout = Abc_NtkObj( pObj->pNtk, Vec_IntEntry(&pObj->vFanouts, i) );
        if ( pFanout == NULL )
            continue;
        if ( pObj->reqT > pFanout->reqT - (Abc_ObjIsCo(pFanout) ? 0 : 1) )
            pObj->reqT = pFanout->reqT - (Abc_ObjIsCo(pFanout) ? 0 : 1);
        if ( pObj->reqG > pFanout->reqG - (Abc_ObjIsCo(pFanout) ? 0.0f : Abc_RefObjWireDelay( pObj, pFanout )) )
            pObj->reqG = pFanout->reqG - (Abc_ObjIsCo(pFanout) ? 0.0f : Abc_RefObjWireDelay( pObj, pFanout ));
    }
}

static void Abc_RefCutUpdateTiming( Abc_Obj_t * pRoot, Vec_Ptr_t * vNodes, Vec_Ptr_t * vSeen )
{
    Abc_Obj_t * pObj;
    int i;

    Vec_PtrClear( vNodes );
    Vec_PtrClear( vSeen );
    Abc_RefCutCollectNodes_rec( pRoot, vNodes, vSeen );
    Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pObj, i )
        Abc_RefCutUpdateArrival( pObj );
    Vec_PtrForEachEntryReverse( Abc_Obj_t *, vNodes, pObj, i )
        Abc_RefCutUpdateRequired( pObj );
}

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Returns function of the cone.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
word * Abc_NodeConeTruth( Vec_Ptr_t * vVars, Vec_Ptr_t * vFuncs, int nWordsMax, Abc_Obj_t * pRoot, Vec_Ptr_t * vLeaves, Vec_Ptr_t * vVisited )
{
    Abc_Obj_t * pNode;
    word * pTruth0, * pTruth1, * pTruth = NULL;
    int i, k, nWords = Abc_Truth6WordNum( Vec_PtrSize(vLeaves) );
    // get nodes in the cut without fanins in the DFS order
    Abc_NodeConeCollect( &pRoot, 1, vLeaves, vVisited, 0 );
    // set elementary functions
    Vec_PtrForEachEntry( Abc_Obj_t *, vLeaves, pNode, i )
        pNode->pCopy = (Abc_Obj_t *)Vec_PtrEntry( vVars, i );
    // prepare functions
    for ( i = Vec_PtrSize(vFuncs); i < Vec_PtrSize(vVisited); i++ )
        Vec_PtrPush( vFuncs, ABC_ALLOC(word, nWordsMax) );
    // compute functions for the collected nodes
    Vec_PtrForEachEntry( Abc_Obj_t *, vVisited, pNode, i )
    {
        assert( !Abc_ObjIsPi(pNode) );
        pTruth0 = (word *)Abc_ObjFanin0(pNode)->pCopy;
        pTruth1 = (word *)Abc_ObjFanin1(pNode)->pCopy;
        pTruth  = (word *)Vec_PtrEntry( vFuncs, i );
        if ( Abc_ObjFaninC0(pNode) )
        {
            if ( Abc_ObjFaninC1(pNode) )
                for ( k = 0; k < nWords; k++ )
                    pTruth[k] = ~pTruth0[k] & ~pTruth1[k];
            else
                for ( k = 0; k < nWords; k++ )
                    pTruth[k] = ~pTruth0[k] &  pTruth1[k];
        }
        else
        {
            if ( Abc_ObjFaninC1(pNode) )
                for ( k = 0; k < nWords; k++ )
                    pTruth[k] =  pTruth0[k] & ~pTruth1[k];
            else
                for ( k = 0; k < nWords; k++ )
                    pTruth[k] =  pTruth0[k] &  pTruth1[k];
        }
        pNode->pCopy = (Abc_Obj_t *)pTruth;
    }
    return pTruth;
}
int Abc_NodeConeIsConst0( word * pTruth, int nVars )
{
    int k, nWords = Abc_Truth6WordNum( nVars );
    for ( k = 0; k < nWords; k++ )
        if ( pTruth[k] )
            return 0;
    return 1;
}
int Abc_NodeConeIsConst1( word * pTruth, int nVars )
{
    int k, nWords = Abc_Truth6WordNum( nVars );
    for ( k = 0; k < nWords; k++ )
        if ( ~pTruth[k] )
            return 0;
    return 1;
}


/**Function*************************************************************

  Synopsis    [Resynthesizes the node using refactoring.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Dec_Graph_t * Abc_NodeRefactor( Abc_ManRef_t * p, Abc_Obj_t * pNode, Vec_Ptr_t * vFanins, int nMinSaved, int fUpdateLevel, int fUseZeros, int fUseDcs, int fVerbose )
{
    extern int    Dec_GraphToNetworkCount( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int NodeMax, int LevelMax );
    int fVeryVerbose = 0;
    int nVars = Vec_PtrSize(vFanins);
    int nWordsMax = Abc_Truth6WordNum(p->nNodeSizeMax);
    Dec_Graph_t * pFForm;
    Abc_Obj_t * pFanin;
    word * pTruth;
    abctime clk;
    int i, nNodesSaved, nNodesAdded, Required;
    if ( fUseZeros )
        nMinSaved = 0;

    p->nNodesConsidered++;

    Required = fUpdateLevel? Abc_ObjRequiredLevel(pNode) : ABC_INFINITY;

    // get the function of the cut
clk = Abc_Clock();
    pTruth = Abc_NodeConeTruth( p->vVars, p->vFuncs, nWordsMax, pNode, vFanins, p->vVisited );
p->timeTru += Abc_Clock() - clk;
    if ( pTruth == NULL )
        return NULL;

    // always accept the case of constant node
    if ( Abc_NodeConeIsConst0(pTruth, nVars) || Abc_NodeConeIsConst1(pTruth, nVars) )
    {
        p->nLastGain = Abc_NodeMffcSize( pNode );
        p->nNodesGained += p->nLastGain;
        p->nNodesRefactored++;
        return Abc_NodeConeIsConst0(pTruth, nVars) ? Dec_GraphCreateConst0() : Dec_GraphCreateConst1();
    }

    // get the factored form
clk = Abc_Clock();
    pFForm = (Dec_Graph_t *)Kit_TruthToGraph( (unsigned *)pTruth, nVars, p->vMemory );
p->timeFact += Abc_Clock() - clk;

    // mark the fanin boundary 
    // (can mark only essential fanins, belonging to bNodeFunc!)
    Vec_PtrForEachEntry( Abc_Obj_t *, vFanins, pFanin, i )
        pFanin->vFanouts.nSize++;
    // label MFFC with current traversal ID
    Abc_NtkIncrementTravId( pNode->pNtk );
    nNodesSaved = Abc_NodeMffcLabelAig( pNode );
    // unmark the fanin boundary and set the fanins as leaves in the form
    Vec_PtrForEachEntry( Abc_Obj_t *, vFanins, pFanin, i )
    {
        pFanin->vFanouts.nSize--;
        Dec_GraphNode(pFForm, i)->pFunc = pFanin;
    }

    // detect how many new nodes will be added (while taking into account reused nodes)
clk = Abc_Clock();
    nNodesAdded = Dec_GraphToNetworkCount( pNode, pFForm, nNodesSaved, Required );
p->timeEval += Abc_Clock() - clk;
    // quit if there is no improvement
    //if ( nNodesAdded == -1 || (nNodesAdded == nNodesSaved && !fUseZeros) )
    if ( nNodesAdded == -1 || nNodesSaved - nNodesAdded < nMinSaved )
    {
        Dec_GraphFree( pFForm );
        return NULL;
    }

    // compute the total gain in the number of nodes
    p->nLastGain = nNodesSaved - nNodesAdded;
    p->nNodesGained += p->nLastGain;
    p->nNodesRefactored++;

    // report the progress
    if ( fVeryVerbose )
    {
        printf( "Node %6s : ",  Abc_ObjName(pNode) );
        printf( "Cone = %2d. ", vFanins->nSize );
        printf( "FF = %2d. ",   1 + Dec_GraphNodeNum(pFForm) );
        printf( "MFFC = %2d. ", nNodesSaved );
        printf( "Add = %2d. ",  nNodesAdded );
        printf( "GAIN = %2d. ", p->nLastGain );
        printf( "\n" );
    }
    return pFForm;
}

Dec_Graph_t * Abc_NodeRefactorP( Abc_ManRef_t * p, Abc_Obj_t * pNode, Vec_Ptr_t * vFanins, int nMinSaved, int fUpdateLevel, int fUseZeros, int fUseDcs, int fVerbose, int fClustering )
{
    extern int    Dec_GraphToNetworkCount( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int NodeMax, int LevelMax );
    extern float  Dec_GraphToNetworkHPWL( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int LevelMax, float overflow, float overflowBefore, float overflowAfter, int fSimple, int fClustering,
        int * pRootArrivalT, int * pRootReqT, float * pRootArrivalG, float * pRootReqG );
    int fVeryVerbose = 0;
    int nVars = Vec_PtrSize(vFanins);
    int nWordsMax = Abc_Truth6WordNum(p->nNodeSizeMax);
    Dec_Graph_t * pFForm;
    Abc_Obj_t * pFanin;
    word * pTruth;
    abctime clk;
    int i, nNodesSaved, nNodesAdded, Required;
    int RootArrivalT, RootReqT, SlackTCur, SlackTNew;

    float HPWLAdded;
    float HPWLSaved;
    float overflowSaved;
    Abc_Ntk_t * pNtk = pNode->pNtk;
    float RootArrivalG, RootReqG, SlackGCur, SlackGNew;
    float GScale, MinSlackNorm, MinSlackNormBase, TimingPartBase, TimingPartCur, HpwlPartCur;
    float ScoreCur, EffectiveScore, AlphaHpwl = 0.2f;
    Vec_Ptr_t * vCutNodes = Vec_PtrAlloc( 32 );
    Vec_Ptr_t * vCutSeen = Vec_PtrAlloc( 32 );

    if ( fUseZeros )
        nMinSaved = 0;

    p->nNodesConsidered++;

    Required = fUpdateLevel? Abc_ObjRequiredLevel(pNode) : ABC_INFINITY;

    // get the function of the cut
clk = Abc_Clock();
    pTruth = Abc_NodeConeTruth( p->vVars, p->vFuncs, nWordsMax, pNode, vFanins, p->vVisited );
p->timeTru += Abc_Clock() - clk;
    if ( pTruth == NULL )
    {
        Vec_PtrFree( vCutSeen );
        Vec_PtrFree( vCutNodes );
        return NULL;
    }

    // always accept the case of constant node
    if ( Abc_NodeConeIsConst0(pTruth, nVars) || Abc_NodeConeIsConst1(pTruth, nVars) )
    {
        Vec_PtrFree( vCutSeen );
        Vec_PtrFree( vCutNodes );
        p->nLastGain = Abc_NodeMffcSize( pNode );
        p->nNodesGained += p->nLastGain;
        p->nNodesRefactored++;
        return Abc_NodeConeIsConst0(pTruth, nVars) ? Dec_GraphCreateConst0() : Dec_GraphCreateConst1();
    }

    // get the factored form
clk = Abc_Clock();
    pFForm = (Dec_Graph_t *)Kit_TruthToGraph( (unsigned *)pTruth, nVars, p->vMemory );
p->timeFact += Abc_Clock() - clk;

    // mark the fanin boundary 
    // (can mark only essential fanins, belonging to bNodeFunc!)
    Vec_PtrForEachEntry( Abc_Obj_t *, vFanins, pFanin, i )
        // pFanin->vFanouts.nSize++;
    {
        Abc_ObjRegular(pFanin)->vFanouts.nSize++;
        Abc_ObjRegular(pFanin)->marked = 1;
        // printf("%d, ", Abc_ObjRegular(pFanin)->Id);
    }
    // label MFFC with current traversal ID
    Abc_NtkIncrementTravId( pNode->pNtk );
    pNode->pNtk->nPosTmp = 0;
    HPWLSaved = Abc_NodeMffcLabelAigHPWL( pNode, &overflowSaved, fClustering );
    nNodesSaved = Abc_NodeMffcLabelAig( pNode );
    Abc_RefCutUpdateTiming( pNode, vCutNodes, vCutSeen );
    // unmark the fanin boundary and set the fanins as leaves in the form
    Vec_PtrForEachEntry( Abc_Obj_t *, vFanins, pFanin, i )
    {
        Abc_ObjRegular(pFanin)->vFanouts.nSize--;
        Abc_ObjRegular(pFanin)->marked = 0;
        Dec_GraphNode(pFForm, i)->pFunc = pFanin;
    }

    // detect how many new nodes will be added (while taking into account reused nodes)
clk = Abc_Clock();
    nNodesAdded = Dec_GraphToNetworkCount( pNode, pFForm, nNodesSaved, Required );
    if ( nNodesAdded == -1 )
    {
        Vec_PtrFree( vCutSeen );
        Vec_PtrFree( vCutNodes );
        Dec_GraphFree( pFForm );
        return NULL;
    }
    HPWLAdded = Dec_GraphToNetworkHPWL( pNode, pFForm, Required, 0, 0, 0, 1, fClustering,
        &RootArrivalT, &RootReqT, &RootArrivalG, &RootReqG );
p->timeEval += Abc_Clock() - clk;

    // binDensity recovery
    float xMin, yMin, xMax, yMax;
    int xb0, xb1, yb0, yb1;
    float x0, x1, y0, y1;
    int nShareCap = 10;
    float xShare[nShareCap], yShare[nShareCap];
    Abc_Obj_t * pNodeTmp;
    assert( pNtk->nPosTmp <= 1000 );
    for (int i = 0; i < pNtk->nPosTmp; ++i)
    {
        pNodeTmp = Abc_NtkObj(pNtk, pNtk->idTmp[i]);
        assert( pNodeTmp != NULL );
        xMin = pNodeTmp->xPos - pNodeTmp->half_den_sizeX;
        yMin = pNodeTmp->yPos - pNodeTmp->half_den_sizeY;
        xMax = pNodeTmp->xPos + pNodeTmp->half_den_sizeX;
        yMax = pNodeTmp->yPos + pNodeTmp->half_den_sizeY;
        xb0 = (int)((xMin - pNtk->binOriX) / pNtk->binStepX);
        yb0 = (int)((yMin - pNtk->binOriY) / pNtk->binStepY);
        xb1 = (int)((xMax - pNtk->binOriX) / pNtk->binStepX);
        yb1 = (int)((yMax - pNtk->binOriY) / pNtk->binStepY);
        if (xb0 < 0)
            xb0 = 0;
        if (yb0 < 0)
            yb0 = 0;
        if (xb1 >= pNtk->binDimX)
            xb1 = pNtk->binDimX - 1;
        if (yb1 >= pNtk->binDimY)
            yb1 = pNtk->binDimY - 1;
        
        for( int xIdx = xb0; xIdx <= xb1; xIdx++) {
            x0 = xIdx * pNtk->binStepX + pNtk->binOriX;

            x0 = fmaxf(x0, xMin);

            x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;

            x1 = fminf(x1, xMax);

            assert( xIdx - xb0 < nShareCap );
            xShare[xIdx - xb0] = x1 - x0;
        }

        for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
            y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

            y0 = fmaxf(y0, yMin);

            y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

            y1 = fminf(y1, yMax);

            assert( yIdx - yb0 < nShareCap );
            yShare[yIdx - yb0] = y1 - y0;
        }


        for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
            for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeTmp->den_scal;
            }
        }
    }

    SlackTCur = pNode->reqT - pNode->arrivalT;
    SlackGCur = pNode->reqG - pNode->arrivalG;
    SlackTNew = RootReqT - RootArrivalT;
    SlackGNew = RootReqG - RootArrivalG;
    GScale = pNode->pNtk->timingGScale > 1.0e-4f ? pNode->pNtk->timingGScale : 1.0f;
    MinSlackNormBase = Abc_MinFloat( (float)SlackTCur, SlackGCur / GScale );
    if ( MinSlackNormBase < -0.25f )
        TimingPartBase = -2.0f;
    else if ( MinSlackNormBase < 0.0f )
        TimingPartBase = -1.0f;
    else if ( MinSlackNormBase < 0.5f )
        TimingPartBase = 0.0f;
    else if ( MinSlackNormBase < 1.0f )
        TimingPartBase = 1.0f;
    else
        TimingPartBase = 2.0f;
    MinSlackNorm = Abc_MinFloat( (float)SlackTNew, SlackGNew / GScale );
    if ( MinSlackNorm < -0.25f )
        TimingPartCur = -2.0f;
    else if ( MinSlackNorm < 0.0f )
        TimingPartCur = -1.0f;
    else if ( MinSlackNorm < 0.5f )
        TimingPartCur = 0.0f;
    else if ( MinSlackNorm < 1.0f )
        TimingPartCur = 1.0f;
    else
        TimingPartCur = 2.0f;
    HpwlPartCur = (HPWLSaved - HPWLAdded) / GScale;
    ScoreCur = (TimingPartCur - TimingPartBase) + (float)(nNodesSaved - nNodesAdded) + AlphaHpwl * HpwlPartCur;
    EffectiveScore = ScoreCur;

    // quit if there is no improvement
    if ( nNodesAdded == -1 || HPWLAdded == -1 || MinSlackNorm < -0.5f
        || nNodesSaved - nNodesAdded < 0 || EffectiveScore <= 0.0f )
    {
        Vec_PtrFree( vCutSeen );
        Vec_PtrFree( vCutNodes );
        Dec_GraphFree( pFForm );
        return NULL;
    }
    // printf("%d, %d, %f, %f\n", nNodesAdded, nNodesSaved, HPWLAdded, HPWLSaved);
    // mark the fanin boundary 
    Vec_PtrForEachEntry( Abc_Obj_t *, vFanins, pFanin, i )
        // Abc_ObjRegular(pFanin)->vFanouts.nSize++;
    {
        Abc_ObjRegular(pFanin)->vFanouts.nSize++;
        Abc_ObjRegular(pFanin)->marked = 1;
        // printf("%d, ", Abc_ObjRegular(pFanin)->Id);
    }

    // label MFFC with current ID
    Abc_NtkIncrementTravId( pNode->pNtk );
    pNode->pNtk->nPosTmp = 0;
    HPWLSaved = Abc_NodeMffcLabelAigHPWL( pNode, &overflowSaved, fClustering );
    // unmark the fanin boundary
    Vec_PtrForEachEntry( Abc_Obj_t *, vFanins, pFanin, i )
        // Abc_ObjRegular(pFanin)->vFanouts.nSize--;
    {
        Abc_ObjRegular(pFanin)->vFanouts.nSize--;
        Abc_ObjRegular(pFanin)->marked = 0;
    }

    // compute the total gain in the number of nodes
    p->nLastGain = nNodesSaved - nNodesAdded;
    p->nNodesGained += p->nLastGain;
    p->nNodesRefactored++;
    Vec_PtrFree( vCutSeen );
    Vec_PtrFree( vCutNodes );

    // report the progress
    if ( fVeryVerbose )
    {
        printf( "Node %6s : ",  Abc_ObjName(pNode) );
        printf( "Cone = %2d. ", vFanins->nSize );
        printf( "FF = %2d. ",   1 + Dec_GraphNodeNum(pFForm) );
        printf( "MFFC = %2d. ", nNodesSaved );
        printf( "Add = %2d. ",  nNodesAdded );
        printf( "GAIN = %2d. ", p->nLastGain );
        printf( "\n" );
    }
    return pFForm;
}

/**Function*************************************************************

  Synopsis    [Starts the resynthesis manager.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Abc_ManRef_t * Abc_NtkManRefStart( int nNodeSizeMax, int nConeSizeMax, int fUseDcs, int fVerbose )
{
    Abc_ManRef_t * p;
    p = ABC_ALLOC( Abc_ManRef_t, 1 );
    memset( p, 0, sizeof(Abc_ManRef_t) );
    p->vCube        = Vec_StrAlloc( 100 );
    p->vVisited     = Vec_PtrAlloc( 100 );
    p->nNodeSizeMax = nNodeSizeMax;
    p->nConeSizeMax = nConeSizeMax;
    p->fVerbose     = fVerbose;
    p->vVars        = Vec_PtrAllocTruthTables( Abc_MaxInt(nNodeSizeMax, 6) );
    p->vFuncs       = Vec_PtrAlloc( 100 );
    p->vMemory      = Vec_IntAlloc( 1 << 16 );
    return p;
}

/**Function*************************************************************

  Synopsis    [Stops the resynthesis manager.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NtkManRefStop( Abc_ManRef_t * p )
{
    Vec_PtrFreeFree( p->vFuncs );
    Vec_PtrFree( p->vVars );
    Vec_IntFree( p->vMemory );
    Vec_PtrFree( p->vVisited );
    Vec_StrFree( p->vCube );
    ABC_FREE( p );
}

/**Function*************************************************************

  Synopsis    [Stops the resynthesis manager.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NtkManRefPrintStats( Abc_ManRef_t * p )
{
    printf( "Refactoring statistics:\n" );
    printf( "Nodes considered  = %8d.\n", p->nNodesConsidered );
    printf( "Nodes refactored  = %8d.\n", p->nNodesRefactored );
    printf( "Gain              = %8d. (%6.2f %%).\n", p->nNodesBeg-p->nNodesEnd, 100.0*(p->nNodesBeg-p->nNodesEnd)/p->nNodesBeg );
    ABC_PRT( "Cuts       ", p->timeCut );
    ABC_PRT( "Resynthesis", p->timeRes );
    ABC_PRT( "    BDD    ", p->timeTru );
    ABC_PRT( "    DCs    ", p->timeDcs );
    ABC_PRT( "    SOP    ", p->timeSop );
    ABC_PRT( "    FF     ", p->timeFact );
    ABC_PRT( "    Eval   ", p->timeEval );
    ABC_PRT( "AIG update ", p->timeNtk );
    ABC_PRT( "TOTAL      ", p->timeTotal );
}

/**Function*************************************************************

  Synopsis    [Performs incremental resynthesis of the AIG.]

  Description [Starting from each node, computes a reconvergence-driven cut, 
  derives BDD of the cut function, constructs ISOP, factors the ISOP, 
  and replaces the current implementation of the MFFC of the node by the 
  new factored form, if the number of AIG nodes is reduced and the total
  number of levels of the AIG network is not increated. Returns the
  number of AIG nodes saved.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NtkRefactor( Abc_Ntk_t * pNtk, int nNodeSizeMax, int nMinSaved, int nConeSizeMax, int fUpdateLevel, int fUseZeros, int fUseDcs, int fVerbose )
{
    extern int           Dec_GraphUpdateNetwork( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain );
    ProgressBar * pProgress;
    Abc_ManRef_t * pManRef;
    Abc_ManCut_t * pManCut;
    Dec_Graph_t * pFForm;
    Vec_Ptr_t * vFanins;
    Abc_Obj_t * pNode;
    abctime clk, clkStart = Abc_Clock();
    int i, nNodes, RetValue = 1;

    assert( Abc_NtkIsStrash(pNtk) );
    // cleanup the AIG
    Abc_AigCleanup((Abc_Aig_t *)pNtk->pManFunc);
    // start the managers
    pManCut = Abc_NtkManCutStart( nNodeSizeMax, nConeSizeMax, 2, 1000 );
    pManRef = Abc_NtkManRefStart( nNodeSizeMax, nConeSizeMax, fUseDcs, fVerbose );
    pManRef->vLeaves   = Abc_NtkManCutReadCutLarge( pManCut );
    // compute the reverse levels if level update is requested
    if ( fUpdateLevel )
        Abc_NtkStartReverseLevels( pNtk, 0 );

    // resynthesize each node once
    pManRef->nNodesBeg = Abc_NtkNodeNum(pNtk);
    nNodes = Abc_NtkObjNumMax(pNtk);
    pProgress = Extra_ProgressBarStart( stdout, nNodes );
    Abc_NtkForEachNode( pNtk, pNode, i )
    {
        Extra_ProgressBarUpdate( pProgress, i, NULL );
        // skip the constant node
//        if ( Abc_NodeIsConst(pNode) )
//            continue;
        // skip persistant nodes
        if ( Abc_NodeIsPersistant(pNode) )
            continue;
        // skip the nodes with many fanouts
        if ( Abc_ObjFanoutNum(pNode) > 1000 )
            continue;
        // stop if all nodes have been tried once
        if ( i >= nNodes )
            break;
        // compute a reconvergence-driven cut
clk = Abc_Clock();
        vFanins = Abc_NodeFindCut( pManCut, pNode, fUseDcs );
pManRef->timeCut += Abc_Clock() - clk;
        // evaluate this cut
clk = Abc_Clock();
        pFForm = Abc_NodeRefactor( pManRef, pNode, vFanins, nMinSaved, fUpdateLevel, fUseZeros, fUseDcs, fVerbose );
pManRef->timeRes += Abc_Clock() - clk;
        if ( pFForm == NULL )
            continue;
        // acceptable replacement found, update the graph
clk = Abc_Clock();
        if ( !Dec_GraphUpdateNetwork( pNode, pFForm, fUpdateLevel, pManRef->nLastGain ) )
        {
            Dec_GraphFree( pFForm );
            RetValue = -1;
            break;
        }
pManRef->timeNtk += Abc_Clock() - clk;
        Dec_GraphFree( pFForm );
    }
    Extra_ProgressBarStop( pProgress );
pManRef->timeTotal = Abc_Clock() - clkStart;
    pManRef->nNodesEnd = Abc_NtkNodeNum(pNtk);

    // print statistics of the manager
    if ( fVerbose )
        Abc_NtkManRefPrintStats( pManRef );
    // delete the managers
    Abc_NtkManCutStop( pManCut );
    Abc_NtkManRefStop( pManRef );
    // put the nodes into the DFS order and reassign their IDs
    Abc_NtkReassignIds( pNtk );
//    Abc_AigCheckFaninOrder( pNtk->pManFunc );
    if ( RetValue != -1 )
    {
        // fix the levels
        if ( fUpdateLevel )
            Abc_NtkStopReverseLevels( pNtk );
        else
            Abc_NtkLevel( pNtk );
        // check
        if ( !Abc_NtkCheck( pNtk ) )
        {
            printf( "Abc_NtkRefactor: The network check has failed.\n" );
            return 0;
        }
    }
    return RetValue;
}


int Abc_NtkRefactor2( Abc_Ntk_t * pNtk, int nNodeSizeMax, int nMinSaved, int nConeSizeMax, int fUpdateLevel, int fUseZeros, int fUseDcs, int fVerbose, int fPlace, int fClustering )
{
    extern int           Dec_GraphUpdateNetwork( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain );
    extern int           Dec_GraphUpdateNetworkP( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain, int fSimple, int fClustering );   //#####################
    ProgressBar * pProgress;
    Abc_ManRef_t * pManRef;
    Abc_ManCut_t * pManCut;
    Dec_Graph_t * pFForm;
    Vec_Ptr_t * vFanins;
    Abc_Obj_t * pNode;
    abctime clk, clkStart = Abc_Clock();
    int i, nNodes, RetValue = 1;
    float nGainHPWL; //#####################

    assert( Abc_NtkIsStrash(pNtk) );
    // cleanup the AIG
    Abc_AigCleanup((Abc_Aig_t *)pNtk->pManFunc);
    if ( fPlace )
        Abc_NtkRefactorPrecomputeTiming( pNtk );
    // start the managers
    pManCut = Abc_NtkManCutStart( nNodeSizeMax, nConeSizeMax, 2, 1000 );
    pManRef = Abc_NtkManRefStart( nNodeSizeMax, nConeSizeMax, fUseDcs, fVerbose );
    pManRef->vLeaves   = Abc_NtkManCutReadCutLarge( pManCut );
    // compute the reverse levels if level update is requested
    if ( fUpdateLevel )
        Abc_NtkStartReverseLevels( pNtk, 0 );

    // resynthesize each node once
    pManRef->nNodesBeg = Abc_NtkNodeNum(pNtk);
    nNodes = Abc_NtkObjNumMax(pNtk);
    pProgress = Extra_ProgressBarStart( stdout, nNodes );
    Abc_NtkForEachNode( pNtk, pNode, i )
    {
        Extra_ProgressBarUpdate( pProgress, i, NULL );
        // skip the constant node
//        if ( Abc_NodeIsConst(pNode) )
//            continue;
        // skip persistant nodes
        if ( Abc_NodeIsPersistant(pNode) )
            continue;
        // skip the nodes with many fanouts
        if ( Abc_ObjFanoutNum(pNode) > 1000 )
            continue;
        // stop if all nodes have been tried once
        if ( i >= nNodes )
            break;
        // compute a reconvergence-driven cut
clk = Abc_Clock();
        vFanins = Abc_NodeFindCut( pManCut, pNode, fUseDcs );
pManRef->timeCut += Abc_Clock() - clk;
        // evaluate this cut
clk = Abc_Clock();

        if (fPlace)
            pFForm = Abc_NodeRefactorP( pManRef, pNode, vFanins, nMinSaved, fUpdateLevel, fUseZeros, fUseDcs, fVerbose, fClustering );
        else
            pFForm = Abc_NodeRefactor( pManRef, pNode, vFanins, nMinSaved, fUpdateLevel, fUseZeros, fUseDcs, fVerbose );

pManRef->timeRes += Abc_Clock() - clk;
        if ( pFForm == NULL )
            continue;
        // acceptable replacement found, update the graph
clk = Abc_Clock();
        if (fPlace)
        {
            if ( !Dec_GraphUpdateNetworkP( pNode, pFForm, fUpdateLevel, pManRef->nLastGain, 1, fClustering ) )
            {
                Dec_GraphFree( pFForm );
                RetValue = -1;
                break;
            }
        }
        else if ( !Dec_GraphUpdateNetwork( pNode, pFForm, fUpdateLevel, pManRef->nLastGain ) )
        {
            Dec_GraphFree( pFForm );
            RetValue = -1;
            break;
        }
pManRef->timeNtk += Abc_Clock() - clk;
        Dec_GraphFree( pFForm );
    }
    Extra_ProgressBarStop( pProgress );
pManRef->timeTotal = Abc_Clock() - clkStart;
    pManRef->nNodesEnd = Abc_NtkNodeNum(pNtk);

    // print statistics of the manager
    if ( fVerbose )
        Abc_NtkManRefPrintStats( pManRef );
    // delete the managers
    Abc_NtkManCutStop( pManCut );
    Abc_NtkManRefStop( pManRef );
    // put the nodes into the DFS order and reassign their IDs
    Abc_NtkReassignIds( pNtk );
//    Abc_AigCheckFaninOrder( pNtk->pManFunc );
    if ( RetValue != -1 )
    {
        // fix the levels
        if ( fUpdateLevel )
            Abc_NtkStopReverseLevels( pNtk );
        else
            Abc_NtkLevel( pNtk );
        // check
        if ( !Abc_NtkCheck( pNtk ) )
        {
            printf( "Abc_NtkRefactor: The network check has failed.\n" );
            return 0;
        }
    }
    return RetValue;
}
////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END
