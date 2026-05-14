/**CFile****************************************************************

  FileName    [decAbc.c]

  PackageName [MVSIS 2.0: Multi-valued logic synthesis system.]

  Synopsis    [Interface between the decomposition package and ABC network.]

  Author      [MVSIS Group]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - February 1, 2003.]

  Revision    [$Id: decAbc.c,v 1.1 2003/05/22 19:20:05 alanmi Exp $]

***********************************************************************/

#include "base/abc/abc.h"
#include "aig/ivy/ivy.h"
#include "dec.h"
#include <float.h>
#include "math.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

typedef struct Dec_TimingSave_t_
{
    Abc_Obj_t *       pObj;
    int               arrivalT;
    int               reqT;
    float             arrivalG;
    float             reqG;
} Dec_TimingSave_t;

#define DEC_TIMING_SAVE_MAX 4096

static inline float Dec_ObjPosXVirtual( Abc_Obj_t * pObj )
{
    return pObj->rawPos ? pObj->xPosTmp : pObj->xPos;
}

static inline float Dec_ObjPosYVirtual( Abc_Obj_t * pObj )
{
    return pObj->rawPos ? pObj->yPosTmp : pObj->yPos;
}

static inline float Dec_ObjWireDelayVirtual( Abc_Obj_t * pFrom, Abc_Obj_t * pTo )
{
    float dx, dy;
    if ( Abc_ObjIsTerm(pFrom) || Abc_ObjIsTerm(pTo) )
        return 0.0f;
    dx = Dec_ObjPosXVirtual(pFrom) - Dec_ObjPosXVirtual(pTo);
    dy = Dec_ObjPosYVirtual(pFrom) - Dec_ObjPosYVirtual(pTo);
    return sqrtf( dx * dx + dy * dy );
}

static int Dec_TimingSaveAddUnique( Dec_TimingSave_t * pSaves, int nSaves, Abc_Obj_t * pObj )
{
    int i;
    pObj = Abc_ObjRegular(pObj);
    for ( i = 0; i < nSaves; ++i )
        if ( pSaves[i].pObj == pObj )
            return nSaves;
    pSaves[nSaves].pObj = pObj;
    pSaves[nSaves].arrivalT = pObj->arrivalT;
    pSaves[nSaves].reqT = pObj->reqT;
    pSaves[nSaves].arrivalG = pObj->arrivalG;
    pSaves[nSaves].reqG = pObj->reqG;
    return nSaves + 1;
}

static void Dec_TimingRestore( Dec_TimingSave_t * pSaves, int nSaves )
{
    int i;
    for ( i = 0; i < nSaves; ++i )
    {
        pSaves[i].pObj->arrivalT = pSaves[i].arrivalT;
        pSaves[i].pObj->reqT = pSaves[i].reqT;
        pSaves[i].pObj->arrivalG = pSaves[i].arrivalG;
        pSaves[i].pObj->reqG = pSaves[i].reqG;
    }
}

static void Dec_TimingUpdateArrivalVirtual( Abc_Obj_t * pObj )
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
        float ArrivalG = pFanin->arrivalG + Dec_ObjWireDelayVirtual( pFanin, pObj );
        if ( pObj->arrivalT < ArrivalT )
            pObj->arrivalT = ArrivalT;
        if ( pObj->arrivalG < ArrivalG )
            pObj->arrivalG = ArrivalG;
    }
}

static void Dec_TimingUpdateRequiredVirtual( Abc_Obj_t * pObj, Abc_Obj_t * pRoot, Abc_Obj_t * pRootNew )
{
    Abc_Obj_t * pFanout;
    int i;

    pObj->reqT = ABC_INFINITY;
    pObj->reqG = (float)ABC_INFINITY;

    Abc_ObjForEachFanout( pObj, pFanout, i )
    {
        if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
            continue;
        int ReqT = pFanout->reqT;
        float ReqG = pFanout->reqG;
        if ( !Abc_ObjIsCo(pFanout) )
        {
            ReqT -= 1;
            ReqG -= Dec_ObjWireDelayVirtual( pObj, pFanout );
        }
        if ( pObj->reqT > ReqT )
            pObj->reqT = ReqT;
        if ( pObj->reqG > ReqG )
            pObj->reqG = ReqG;
    }

    if ( pObj == Abc_ObjRegular(pRootNew) )
    {
        Abc_ObjForEachFanout( pRoot, pFanout, i )
        {
            int ReqT = pFanout->reqT;
            float ReqG = pFanout->reqG;
            if ( !Abc_ObjIsCo(pFanout) )
            {
                ReqT -= 1;
                ReqG -= Dec_ObjWireDelayVirtual( pObj, pFanout );
            }
            if ( pObj->reqT > ReqT )
                pObj->reqT = ReqT;
            if ( pObj->reqG > ReqG )
                pObj->reqG = ReqG;
        }
    }
}

static int Dec_TimingUpdateVirtualCandidate(
    Dec_Graph_t * pGraph,
    Abc_Obj_t * pRoot,
    Abc_Obj_t * pRootNew,
    Abc_Obj_t ** pNodesCore,
    int nNodesCore,
    Dec_TimingSave_t * pSaves,
    int * pRootArrivalT,
    int * pRootReqT,
    float * pRootArrivalG,
    float * pRootReqG )
{
    Dec_Node_t * pNode;
    Abc_Obj_t * pObj;
    int i, nSaves = 0;

    pRootNew = Abc_ObjRegular(pRootNew);
    Dec_GraphForEachLeaf( pGraph, pNode, i )
    {
        assert( nSaves < DEC_TIMING_SAVE_MAX );
        nSaves = Dec_TimingSaveAddUnique( pSaves, nSaves, (Abc_Obj_t *)pNode->pFunc );
    }
    for ( i = 0; i < nNodesCore; ++i )
    {
        assert( nSaves < DEC_TIMING_SAVE_MAX );
        nSaves = Dec_TimingSaveAddUnique( pSaves, nSaves, pNodesCore[i] );
    }

    Dec_GraphForEachLeaf( pGraph, pNode, i )
    {
        pObj = Abc_ObjRegular( (Abc_Obj_t *)pNode->pFunc );
        Dec_TimingUpdateArrivalVirtual( pObj );
    }
    for ( i = 0; i < nNodesCore; ++i )
        Dec_TimingUpdateArrivalVirtual( pNodesCore[i] );

    for ( i = nNodesCore - 1; i >= 0; --i )
        Dec_TimingUpdateRequiredVirtual( pNodesCore[i], pRoot, pRootNew );
    Dec_GraphForEachLeaf( pGraph, pNode, i )
    {
        pObj = Abc_ObjRegular( (Abc_Obj_t *)pNode->pFunc );
        Dec_TimingUpdateRequiredVirtual( pObj, pRoot, pRootNew );
    }

    if ( pRootArrivalT )
        *pRootArrivalT = pRootNew->arrivalT;
    if ( pRootReqT )
        *pRootReqT = pRootNew->reqT;
    if ( pRootArrivalG )
        *pRootArrivalG = pRootNew->arrivalG;
    if ( pRootReqG )
        *pRootReqG = pRootNew->reqG;
    return nSaves;
}

static void Dec_TimingUpdateRequiredActual( Abc_Obj_t * pObj, Abc_Obj_t * pRoot, Abc_Obj_t * pRootNew )
{
    Abc_Obj_t * pFanout;
    int i;

    pObj->reqT = ABC_INFINITY;
    pObj->reqG = (float)ABC_INFINITY;

    Abc_ObjForEachFanout( pObj, pFanout, i )
    {
        int ReqT = pFanout->reqT;
        float ReqG = pFanout->reqG;
        if ( !Abc_ObjIsCo(pFanout) )
        {
            ReqT -= 1;
            ReqG -= Dec_ObjWireDelayVirtual( pObj, pFanout );
        }
        if ( pObj->reqT > ReqT )
            pObj->reqT = ReqT;
        if ( pObj->reqG > ReqG )
            pObj->reqG = ReqG;
    }

    if ( pObj == Abc_ObjRegular(pRootNew) )
    {
        Abc_ObjForEachFanout( pRoot, pFanout, i )
        {
            int ReqT = pFanout->reqT;
            float ReqG = pFanout->reqG;
            if ( !Abc_ObjIsCo(pFanout) )
            {
                ReqT -= 1;
                ReqG -= Dec_ObjWireDelayVirtual( pObj, pFanout );
            }
            if ( pObj->reqT > ReqT )
                pObj->reqT = ReqT;
            if ( pObj->reqG > ReqG )
                pObj->reqG = ReqG;
        }
    }
}

static void Dec_TimingUpdateActualCandidate( Dec_Graph_t * pGraph, Abc_Obj_t * pRoot, Abc_Obj_t * pRootNew )
{
    Dec_Node_t * pNode;
    Abc_Obj_t * pObj;
    int i;

    Dec_GraphForEachLeaf( pGraph, pNode, i )
    {
        pObj = Abc_ObjRegular( (Abc_Obj_t *)pNode->pFunc );
        Dec_TimingUpdateArrivalVirtual( pObj );
    }
    Dec_GraphForEachNode( pGraph, pNode, i )
    {
        pObj = Abc_ObjRegular( (Abc_Obj_t *)pNode->pFunc );
        Dec_TimingUpdateArrivalVirtual( pObj );
    }

    for ( i = pGraph->nSize - 1; i >= pGraph->nLeaves; --i )
    {
        pNode = Dec_GraphNode( pGraph, i );
        pObj = Abc_ObjRegular( (Abc_Obj_t *)pNode->pFunc );
        Dec_TimingUpdateRequiredActual( pObj, pRoot, pRootNew );
    }
    Dec_GraphForEachLeaf( pGraph, pNode, i )
    {
        pObj = Abc_ObjRegular( (Abc_Obj_t *)pNode->pFunc );
        Dec_TimingUpdateRequiredActual( pObj, pRoot, pRootNew );
    }
}

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Transforms the decomposition graph into the AIG.]

  Description [AIG nodes for the fanins should be assigned to pNode->pFunc
  of the leaves of the graph before calling this procedure.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Abc_Obj_t * Dec_GraphToNetwork( Abc_Ntk_t * pNtk, Dec_Graph_t * pGraph )
{
    Abc_Obj_t * pAnd0, * pAnd1;
    Dec_Node_t * pNode = NULL; // Suppress "might be used uninitialized"
    int i;
    // check for constant function
    if ( Dec_GraphIsConst(pGraph) )
        return Abc_ObjNotCond( Abc_AigConst1(pNtk), Dec_GraphIsComplement(pGraph) );
    // check for a literal
    if ( Dec_GraphIsVar(pGraph) )
        return Abc_ObjNotCond( (Abc_Obj_t *)Dec_GraphVar(pGraph)->pFunc, Dec_GraphIsComplement(pGraph) );
    // build the AIG nodes corresponding to the AND gates of the graph
    Dec_GraphForEachNode( pGraph, pNode, i )
    {
        pAnd0 = Abc_ObjNotCond( (Abc_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge0.Node)->pFunc, pNode->eEdge0.fCompl ); 
        pAnd1 = Abc_ObjNotCond( (Abc_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge1.Node)->pFunc, pNode->eEdge1.fCompl ); 
        pNode->pFunc = Abc_AigAnd( (Abc_Aig_t *)pNtk->pManFunc, pAnd0, pAnd1 );
        ((Abc_Obj_t *)pNode->pFunc)->rawPos = 0;
    }
    // complement the result if necessary
    return Abc_ObjNotCond( (Abc_Obj_t *)pNode->pFunc, Dec_GraphIsComplement(pGraph) );
}

Abc_Obj_t * Dec_GraphToNetwork2( Abc_Ntk_t * pNtk, Dec_Graph_t * pGraph, Abc_Obj_t * pRoot, int fSimple, int fClustering )
{
    Abc_Aig_t * pMan = (Abc_Aig_t *)pNtk->pManFunc;
    Dec_Node_t * pNode, * pNode0, * pNode1;
    Abc_Obj_t * pAnd, * pAnd0, * pAnd1;
    Abc_Obj_t * pAnd0R, * pAnd1R, * pNodeR;
    Abc_Obj_t * pFanout;
    Abc_Obj_t * pRootNew;
    
    float xMin, xMax, yMin, yMax;
    int added = 0;
    int Counter, LevelNew, LevelOld;
    int i, j;
    float increHPWL1 = 0.0;
    int nNodesNew, nNodesOld, RetValue;
    float increHPWL2 = 0.0, newHPWL = 0.0, oldHPWL = 0.0;

    int nPeriCap = 64;
    Abc_Obj_t * rawNodesCore[3001], * rawNodesPeri[64], * rawNodesExtra[3001];
    Abc_Obj_t * pNodeAig;
    Abc_Obj_t * pFanin, * pFaninOut;
    int k, l, iter = 0, nSizeCore = 0, nSizePeri = 0, nSizeExtra = 0;
    float minXPosFanin, maxXPosFanin, minYPosFanin, maxYPosFanin, minXPosFanout, maxXPosFanout, minYPosFanout, maxYPosFanout;
    float posDiffCore = 10, posDiffPeri = 10, posDiffCorePre = 1000;
    float posXNew, posYNew;
    int nEndPointCap = 10;
    float xPosSet[nEndPointCap], yPosSet[nEndPointCap];
    // float rectLx[100000], rectRx[100000], rectLy[100000], rectRy[100000];
    int nEndPoints;

    int xIdx, yIdx;
    float xOff, yOff;
    int xIdx1, yIdx1;
    float xOff1, yOff1;
    float xOldTmp[3001], yOldTmp[3001], xNewTmp[3001], yNewTmp[3001];
    int idxOldTmp[3001], idxNewTmp[3001];
    int nOld = 0, nNew = 0;
    float minDensity, curDensity;
    int xBest, yBest;
    float xStep, yStep;
    float minOverflow, curOverflow;
    int xCnt, yCnt;
    float halfBinRatio = pNtk->halfBinRatio;
    int nShareCap = 10;

    // float xMin, yMin, xMax, yMax;
    int xb0, xb1, yb0, yb1;
    float x0, x1, y0, y1;
    float xShare[nShareCap], yShare[nShareCap];
    float scaleX, scaleY;
    float xGlobalMin = 100000, xGlobalMax = -100000, yGlobalMin = 100000, yGlobalMax = -100000;

    float totalHPWL1 = 0, totalHPWL2 = 0;

    // check for constant function
    if ( Dec_GraphIsConst(pGraph) )
        return Abc_ObjNotCond( Abc_AigConst1(pNtk), Dec_GraphIsComplement(pGraph) );
    // check for a literal
    if ( Dec_GraphIsVar(pGraph) )
        return Abc_ObjNotCond( (Abc_Obj_t *)Dec_GraphVar(pGraph)->pFunc, Dec_GraphIsComplement(pGraph) );
// float overflow = 0;
//         for (int i = 0; i < pNtk->nBins; ++i)
//         {
//             // printf("%.2f && ", pNtk->binsDensity[i]);
//             overflow += (pNtk->binsDensity[i] > 1) ? pNtk->binsDensity[i] - 1 : 0;
//         }
//         overflow *= pNtk->binModuleRatio;
//         printf("\nOverflow000000 = %f\n", overflow);

        // // check the global HPWL before adding nodes
        // float totalHPWL = 0;
        // int ii, jj;
        // Abc_NtkForEachObj(pNtk, pNodeAig, ii)
        // {
        //     if ( pNodeAig->rawPos == 0 && Abc_NodeIsTravIdCurrent(pNodeAig) )
        //         continue;
        //     // IO floating mode: skip the terminals ****************************
        //     if ( Abc_ObjIsTerm(pNodeAig) )
        //     {
        //         xMin = 1000000;
        //         yMin = 1000000;
        //         xMax = -1000000;
        //         yMax = -1000000;
        //     }
        //     else
        //     {
        //         xMin = xMax = pNodeAig->xPos;
        //         yMin = yMax = pNodeAig->yPos;
        //     }
        //     Abc_ObjForEachFanout( pNodeAig, pFanout, jj )
        //     {
        //         // IO floating mode: skip the terminals ****************************
        //         if ( Abc_ObjIsTerm(pFanout) )
        //             continue;
        //         if (pFanout->Id == 0)
        //             continue;
        //         if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
        //             continue;
        //         xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
        //         yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
        //         xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
        //         yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
        //     }
        //     if ( pNodeAig == Abc_ObjRegular(pRootNew) )
        //     {
        //         Abc_ObjForEachFanout( pRoot, pFanout, jj )
        //         {
        //             // IO floating mode: skip the terminals ****************************
        //             if ( Abc_ObjIsTerm(pFanout) )
        //                 continue;
        //             if (pFanout->Id == 0)
        //                 continue;
        //             // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
        //             xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
        //             yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
        //             xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
        //             yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
        //         }
        //     }
        //     if ( xMin == 1000000 )
        //         continue;
        //     totalHPWL += xMax - xMin + yMax - yMin;
        // }
        // printf("totalHPWLbeforeAdding = %f\n", totalHPWL);

    // calculate the added internal HPWL and collect rawNodesCore
    Dec_GraphForEachNode( pGraph, pNode, i )
    {
        pAnd0 = Abc_ObjNotCond( (Abc_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge0.Node)->pFunc, pNode->eEdge0.fCompl ); 
        pAnd1 = Abc_ObjNotCond( (Abc_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge1.Node)->pFunc, pNode->eEdge1.fCompl ); 
        pNode->pFunc = Abc_AigAnd( pMan, pAnd0, pAnd1 );
        
        pNodeR = Abc_ObjRegular((Abc_Obj_t *)pNode->pFunc);
        pAnd0R = Abc_ObjRegular(pAnd0);
        pAnd1R = Abc_ObjRegular(pAnd1);

        // check if the node is already in the rawNodesCore set
        int flag = 0;
        for (int k = 0; k < nSizeCore; k++)
            if ( rawNodesCore[k]->Id == pNodeR->Id )
            {
                flag = 1;
                break;
            }
        if (flag) continue;
        
        if ( pNodeR->rawPos )
        {
            pNodeR->xPos = ( pAnd0R->xPos + pAnd1R->xPos + pRoot->xPos ) / 3;
            pNodeR->yPos = ( pAnd0R->yPos + pAnd1R->yPos + pRoot->yPos ) / 3;
        }
        if ( pNodeR->rawPos || Abc_NodeIsTravIdCurrent(pNodeR) )
        {
            // HPWL increase after adding the edge between pAnd0 and pNode
            // if pAnd0R is a Ci, ignore the wire
            if ( Abc_ObjIsTerm(pAnd0R) )
            {
                xMin = 1000000;
                yMin = 1000000;
                xMax = -1000000;
                yMax = -1000000;
            }
            else
            {
                xMin = xMax = pAnd0R->xPos;
                yMin = yMax = pAnd0R->yPos;
            }
            Abc_ObjForEachFanout( pAnd0R, pFanout, j)
            {
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if ( pFanout == pNodeR )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                    continue;
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
            if ( pNodeR->xPos < xMin )
                increHPWL1 += xMin - pNodeR->xPos;
            if ( pNodeR->xPos > xMax )
                increHPWL1 += pNodeR->xPos - xMax;
            if ( pNodeR->yPos < yMin )
                increHPWL1 += yMin - pNodeR->yPos;
            if ( pNodeR->yPos > yMax )
                increHPWL1 += pNodeR->yPos - yMax;
            // HPWL increase after adding the edge between pAnd1 and pNode
            // if pAnd1R is a Ci, ignore the wire
            if ( Abc_ObjIsTerm(pAnd1R) )
            {
                xMin = 1000000;
                yMin = 1000000;
                xMax = -1000000;
                yMax = -1000000;
            }
            else
            {
                xMin = xMax = pAnd1R->xPos;
                yMin = yMax = pAnd1R->yPos;
            }
            Abc_ObjForEachFanout( pAnd1R, pFanout, j)
            {
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if ( pFanout == pNodeR )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                    continue;
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
            if ( pNodeR->xPos < xMin )
                increHPWL1 += xMin - pNodeR->xPos;
            if ( pNodeR->xPos > xMax )
                increHPWL1 += pNodeR->xPos - xMax;
            if ( pNodeR->yPos < yMin )
                increHPWL1 += yMin - pNodeR->yPos;
            if ( pNodeR->yPos > yMax )
                increHPWL1 += pNodeR->yPos - yMax;

            // the size of the added node
            pNodeR->half_den_sizeX = (Abc_ObjFaninC0(pNodeR) && Abc_ObjFaninC1(pNodeR)) ? 1 : 1.5;
            // pNodeR->half_den_sizeX = 0.5;
            pNodeR->half_den_sizeY = 0.5;
            // SQRT2 = smoothing parameter for density_size calculation
            if(pNodeR->half_den_sizeX < pNtk->binStepX / 2 * 1.414213562373095) {
                scaleX = pNodeR->half_den_sizeX / (pNtk->binStepX / 2 * 1.414213562373095);
                pNodeR->half_den_sizeX = pNtk->binStepX / 2 * 1.414213562373095;
            }
                else {
                scaleX = 1.0;
            }

            if(pNodeR->half_den_sizeY < pNtk->binStepY / 2 * 1.414213562373095) {
                scaleY = pNodeR->half_den_sizeY / (pNtk->binStepY / 2 * 1.414213562373095);
                pNodeR->half_den_sizeY = pNtk->binStepY / 2 * 1.414213562373095;
            }
                else {
                scaleY = 1.0;
            }
            pNodeR->den_scal = scaleX * scaleY;
        }
        // density change
        else
        {
            assert( nOld < 3001 );
            idxOldTmp[nOld++] = pNodeR->Id; 
            xMin = pNodeR->xPos - pNodeR->half_den_sizeX;
            yMin = pNodeR->yPos - pNodeR->half_den_sizeY;
            xMax = pNodeR->xPos + pNodeR->half_den_sizeX;
            yMax = pNodeR->yPos + pNodeR->half_den_sizeY;
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
                    pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] -= xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeR->den_scal;
                }
            }
        }

        pNodeR->rawPos = 1;
        assert( nSizeCore < 3001 );
        rawNodesCore[nSizeCore++] = pNodeR;
        pNodeR->xPosTmp = pNodeR->xPos;
        pNodeR->yPosTmp = pNodeR->yPos;
        // // update the bounding box
        // xGlobalMin = fminf(xGlobalMin, pNodeR->xPos);
        // xGlobalMax = fmaxf(xGlobalMax, pNodeR->xPos);
        // yGlobalMin = fminf(yGlobalMin, pNodeR->yPos);
        // yGlobalMax = fmaxf(yGlobalMax, pNodeR->yPos);
        // printf("%d, %d, %.2f, %.2f\n", pNodeR->Id, pNodeR->rawPos, pNodeR->xPos, pNodeR->yPos);
    }
    // printf("increHPWL1 = %f\n", increHPWL1);

    // // check the global HPWL after adding nodes
    // totalHPWL = 0;
    // Abc_NtkForEachObj(pNtk, pNodeAig, ii)
    // {
    //     if ( pNodeAig->rawPos == 0 && Abc_NodeIsTravIdCurrent(pNodeAig) )
    //         continue;
    //     // IO floating mode: skip the terminals ****************************
    //     if ( Abc_ObjIsTerm(pNodeAig) )
    //     {
    //         xMin = 1000000;
    //         yMin = 1000000;
    //         xMax = -1000000;
    //         yMax = -1000000;
    //     }
    //     else
    //     {
    //         xMin = xMax = pNodeAig->xPos;
    //         yMin = yMax = pNodeAig->yPos;
    //     }
    //     Abc_ObjForEachFanout( pNodeAig, pFanout, jj )
    //     {
    //         // IO floating mode: skip the terminals ****************************
    //         if ( Abc_ObjIsTerm(pFanout) )
    //             continue;
    //         if (pFanout->Id == 0)
    //             continue;
    //         if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
    //             continue;
    //         xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
    //         yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
    //         xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
    //         yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
    //     }
    //     if ( pNodeAig == Abc_ObjRegular(pRootNew) )
    //     {
    //         Abc_ObjForEachFanout( pRoot, pFanout, jj )
    //         {
    //             // IO floating mode: skip the terminals ****************************
    //             if ( Abc_ObjIsTerm(pFanout) )
    //                 continue;
    //             if (pFanout->Id == 0)
    //                 continue;
    //             // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
    //             xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
    //             yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
    //             xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
    //             yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
    //         }
    //     }
    //     if ( xMin == 1000000 )
    //         continue;
    //     totalHPWL += xMax - xMin + yMax - yMin;
    // }
    // printf("totalHPWLduringAdding = %f\n", totalHPWL);

    // calculate the added external HPWL (between the root and its fanouts)
    pRootNew = Abc_ObjNotCond( (Abc_Obj_t *)pNode->pFunc, Dec_GraphIsComplement(pGraph) );

    xMin = xMax = Abc_ObjRegular(pRootNew)->xPos;
    yMin = yMax = Abc_ObjRegular(pRootNew)->yPos;
    Abc_ObjForEachFanout( Abc_ObjRegular(pRootNew), pFanout, i )
    {
        // IO floating mode: skip the terminals ****************************
        if ( Abc_ObjIsTerm(pFanout) )
            continue;
        if (pFanout->Id == 0)
            continue;
        if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
            continue;
        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
    }
    oldHPWL = xMax - xMin + yMax - yMin;
    Abc_ObjForEachFanout( Abc_ObjRegular(pRoot), pFanout, i )
    {
        // IO floating mode: skip the terminals ****************************
        if ( Abc_ObjIsTerm(pFanout) )
            continue;
        if (pFanout->Id == 0)
            continue;
        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
    }
    newHPWL = xMax - xMin + yMax - yMin;
    increHPWL2 = newHPWL - oldHPWL;
    // printf("newHPWL = %.2f\n", newHPWL);
    // printf("oldHPWL = %.2f\n", oldHPWL);
    // printf("increHPWL2 = %f\n", increHPWL2);


    // // check the global HPWL after adding nodes
    // totalHPWL = 0;
    // Abc_NtkForEachObj(pNtk, pNodeAig, ii)
    // {
    //     if ( pNodeAig->rawPos == 0 && Abc_NodeIsTravIdCurrent(pNodeAig) )
    //         continue;
    //     // IO floating mode: skip the terminals ****************************
    //     if ( Abc_ObjIsTerm(pNodeAig) )
    //     {
    //         xMin = 1000000;
    //         yMin = 1000000;
    //         xMax = -1000000;
    //         yMax = -1000000;
    //     }
    //     else
    //     {
    //         xMin = xMax = pNodeAig->xPos;
    //         yMin = yMax = pNodeAig->yPos;
    //     }
    //     Abc_ObjForEachFanout( pNodeAig, pFanout, jj )
    //     {
    //         // IO floating mode: skip the terminals ****************************
    //         if ( Abc_ObjIsTerm(pFanout) )
    //             continue;
    //         if (pFanout->Id == 0)
    //             continue;
    //         if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
    //             continue;
    //         xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
    //         yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
    //         xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
    //         yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
    //     }
    //     if ( pNodeAig == Abc_ObjRegular(pRootNew) )
    //     {
    //         Abc_ObjForEachFanout( pRoot, pFanout, jj )
    //         {
    //             // IO floating mode: skip the terminals ****************************
    //             if ( Abc_ObjIsTerm(pFanout) )
    //                 continue;
    //             if (pFanout->Id == 0)
    //                 continue;
    //             // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
    //             xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
    //             yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
    //             xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
    //             yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
    //         }
    //     }
    //     if ( xMin == 1000000 )
    //         continue;
    //     totalHPWL += xMax - xMin + yMax - yMin;
    // }
    // printf("totalHPWLafterAdding = %f\n", totalHPWL);

    // printf("Counter = %d\n", Counter);
    // printf("pRootNew->rawPos = %d\n", Abc_ObjRegular(pRootNew)->rawPos);

    // // expand the core to a maximal tree
    // // Abc_Obj_t * addedCoreNodes[10000];
    // int headIdx = 0, tailIdx = 0;
    // int nFanouts = 0;
    // tailIdx = nSizeCore;
    // // for ( i = 0; i < nSizeCore; ++i )
    // // {
    // //     addedCoreNodes[tailIdx++] = rawNodesCore[i];
    // // }
    // while ( headIdx < tailIdx && tailIdx < 10 * nSizeCore )
    // {
    //     pNodeAig = rawNodesCore[headIdx++];
    //     Abc_ObjForEachFanin( pNodeAig, pFanin, j )
    //     {
    //         if ( !Abc_ObjIsTerm(pFanin) && !pFanin->rawPos && !Abc_NodeIsTravIdCurrent(pFanin) )
    //         {
    //             if (pFanin->Id == 0)
    //                 continue;
    //             nFanouts = 0;
    //             Abc_ObjForEachFanout( pFanin, pFaninOut, k )
    //                 if ( pFaninOut->rawPos == 1 )
    //                     nFanouts++;
    //             if ( nFanouts == 1 )
    //             {
    //                 pFanin->rawPos = 1;
    //                 rawNodesCore[tailIdx++] = pFanin;
    //                 // addedCoreNodes[tailIdx++] = pFanin;
    //                 pFanin->xPosTmp = pFanin->xPos;
    //                 pFanin->yPosTmp = pFanin->yPos;
    //             }
    //         }
    //     }
    // }
    // // printf("*********** Update **********\n");
    // // printf("nSizeCore = %d\n", nSizeCore);
    // // reorder the rawNodes
    // for ( i = tailIdx; i < 2 * tailIdx; ++i)
    //     rawNodesCore[i] = rawNodesCore[i - tailIdx];
    // for ( i = 0; i < tailIdx - nSizeCore; ++i )
    //     rawNodesCore[i] = rawNodesCore[2 * tailIdx - i - 1];
    // for ( i = tailIdx - nSizeCore; i < tailIdx; ++i )
    //     rawNodesCore[i] = rawNodesCore[i + nSizeCore];
    // nSizeCore = tailIdx;

    // collect rawNodesPeri which are fanins and their fanouts, and fanouts of rawNodesCore
    // for ( i = 0; i < nSizeCore; ++i )
    // {
    //     pNodeAig = rawNodesCore[i];
    //     // printf("%d & ", pNodeAig->Id);
    //     Abc_ObjForEachFanin( pNodeAig, pFanin, j )
    //     {
    //         if ( !Abc_ObjIsTerm(pFanin) && !pFanin->rawPos && !Abc_NodeIsTravIdCurrent(pFanin) )
    //         {
    //             if (pFanin->Id == 0)
    //                 continue;
    //             rawNodesPeri[nSizePeri++] = pFanin;
    //             pFanin->rawPos = 2;
    //             // printf("%d &&& ", pFanin->Id);
    //         }
    //         Abc_ObjForEachFanout( pFanin, pFaninOut, k )
    //         {
    //             // printf("%d ***** ", pFaninOut->Id);
    //             if ( !Abc_ObjIsTerm(pFaninOut) && !pFaninOut->rawPos && !Abc_NodeIsTravIdCurrent(pFaninOut) )
    //             {
    //                 if (pFaninOut->Id == 0)
    //                     continue;
    //                 // rawNodesPeri[nSizePeri++] = pFaninOut;
    //                 // pFaninOut->rawPos = 2;
    //                 // copy the positions of the fanouts of the fanin
    //                 pFaninOut->xPosTmp = pFaninOut->xPos;
    //                 pFaninOut->yPosTmp = pFaninOut->yPos;
    //                 // printf("%d &&&&& ", pFaninOut->Id);
    //             }
    //         }
    //     }
    //     Abc_ObjForEachFanout( pNodeAig, pFanout, j )
    //         if ( !Abc_ObjIsTerm(pFanout) && !pFanout->rawPos && !Abc_NodeIsTravIdCurrent(pFanout) )
    //         {
    //             if (pFanout->Id == 0)
    //                 continue;
    //             rawNodesPeri[nSizePeri++] = pFanout;
    //             pFanout->rawPos = 2;
    //             // printf("%d *** ", pFaninOut->Id);
    //         }
    //     // printf("\n");
    // }
    // Abc_ObjForEachFanout( pRoot, pFanout, i )     // fanouts of pRoot will be added to pRootNew
    //     if ( !Abc_ObjIsTerm(pFanout) && !pFanout->rawPos )
    //     {
    //         if (pFanout->Id == 0)
    //             continue;
    //         rawNodesPeri[nSizePeri++] = pFanout;
    //         pFanout->rawPos = 2;
    //         // printf("%d^^^\n", pFanout->Id);
    //     }
    // assert ( nSizePeri == 0 );
    for ( i = 0; i < nSizePeri; ++i )
    {
        pNodeAig = rawNodesPeri[i];
        pNodeAig->xPosTmp = pNodeAig->xPos;
        pNodeAig->yPosTmp = pNodeAig->yPos;
        // // update the bounding box
        // xGlobalMin = fminf(xGlobalMin, pNodeAig->xPos);
        // xGlobalMax = fmaxf(xGlobalMax, pNodeAig->xPos);
        // yGlobalMin = fminf(yGlobalMin, pNodeAig->yPos);
        // yGlobalMax = fmaxf(yGlobalMax, pNodeAig->yPos);
        // printf("%d ** ", pNodeAig->Id);
    }
    // printf("*********** Update **********\n");
    // printf("nSizeCore = %d\n", nSizeCore);
    // printf("nSizePeri1 = %d\n", nSizePeri);

    // ########## incremental placement ##########
    // while ( posDiffCore > pow(10.0, -2) && fabsf(posDiffCore - posDiffCorePre) > pow(10.0, -3) )
    {   
        posDiffCorePre = posDiffCore;
        posDiffCore = 0;
        posDiffPeri = 0;
        iter++;
        // Core nodes
        for ( int i = 0; i < nSizeCore; ++i )
        {   
            minXPosFanout = 1000000;
            maxXPosFanout = -1000000;
            minYPosFanout = 1000000;
            maxYPosFanout = -1000000;
            nEndPoints = 0;
            pNodeAig = rawNodesCore[i];
            Abc_ObjForEachFanin( pNodeAig, pFanin, j )
            {
                // IO floating mode: skip the terminals ****************************
                minXPosFanin = 1000000;
                maxXPosFanin = -1000000;
                minYPosFanin = 1000000;
                maxYPosFanin = -1000000;
                if ( !Abc_ObjIsTerm(pFanin) && pFanin->Id != 0 )
                {
                    if ( pFanin->rawPos == 1 )
                    {
                        // skip floating nodes
                        if ( pFanin->rectLx == -10000.56 )
                        {
                            assert ( pFanin->rectRx == -10000.56 );
                            assert ( pFanin->rectLy == -10000.56 );
                            assert ( pFanin->rectRy == -10000.56 );
                        }
                        else
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->rectLx);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->rectLx);
                            minXPosFanin = fminf(minXPosFanin, pFanin->rectRx);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->rectRx);
                            minYPosFanin = fminf(minYPosFanin, pFanin->rectLy);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->rectLy);
                            minYPosFanin = fminf(minYPosFanin, pFanin->rectRy);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->rectRy);
                        }
                    }
                    else
                    {
                        if ( !Abc_ObjIsTerm(pFanin) )
                        if ( pFanin->rawPos == 2 )
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->xPosTmp);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPosTmp);
                            minYPosFanin = fminf(minYPosFanin, pFanin->yPosTmp);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPosTmp);
                        }
                        else
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->xPos);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPos);
                            minYPosFanin = fminf(minYPosFanin, pFanin->yPos);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPos);
                        }
                    }
                }
                Abc_ObjForEachFanout( pFanin, pFaninOut, k )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFaninOut) )
                        continue;
                    if ( pFaninOut->rawPos == 1 || Abc_NodeIsTravIdCurrent(pFaninOut) )
                        continue;
                    if (pFaninOut->Id == 0)
                        continue;
                    // assert( Abc_ObjIsTerm(pFaninOut) || pFaninOut->rawPos == 2 );
                    // if ( !Abc_ObjIsTerm(pFaninOut) )
                    if ( pFaninOut->rawPos == 2 )
                    {
                        minXPosFanin = fminf(minXPosFanin, pFaninOut->xPosTmp);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPosTmp);
                        minYPosFanin = fminf(minYPosFanin, pFaninOut->yPosTmp);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPosTmp);
                    }
                    else
                    {
                        minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
                        minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
                    }
                }
                if (minXPosFanin < 900000)
                {
                    assert( nEndPoints + 1 < nEndPointCap );
                    for ( k = 0; k < nEndPoints; k++ )
                        if ( xPosSet[k] >= minXPosFanin )
                            break;
                    for ( l = nEndPoints; l > k; l-- )
                        xPosSet[l] = xPosSet[l - 1];
                    xPosSet[k] = minXPosFanin;

                    for ( ++k; k <= nEndPoints; k++ )
                        if ( xPosSet[k] >= maxXPosFanin )
                            break;
                    for ( l = nEndPoints + 1; l > k; l-- )
                        xPosSet[l] = xPosSet[l - 1];
                    xPosSet[k] = maxXPosFanin;

                    for ( k = 0; k < nEndPoints; k++ )
                        if ( yPosSet[k] >= minYPosFanin )
                            break;
                    for ( l = nEndPoints; l > k; l-- )
                        yPosSet[l] = yPosSet[l - 1];
                    yPosSet[k] = minYPosFanin;

                    for ( ++k; k <= nEndPoints; k++ )
                        if ( yPosSet[k] >= maxYPosFanin )
                            break;
                    for ( l = nEndPoints + 1; l > k; l-- )
                        yPosSet[l] = yPosSet[l - 1];
                    yPosSet[k] = maxYPosFanin;
                    nEndPoints += 2;
                }
            }
            Abc_ObjForEachFanout( pNodeAig, pFanout, j )
            {
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if ( pFanout->rawPos == 1 || Abc_NodeIsTravIdCurrent(pFanout) )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
                // if ( !Abc_ObjIsTerm(pFanout) )
                if ( pFanout->rawPos == 2 )
                {
                    minXPosFanout = fminf(minXPosFanout, pFanout->xPosTmp);
                    maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPosTmp);
                    minYPosFanout = fminf(minYPosFanout, pFanout->yPosTmp);
                    maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPosTmp);
                }
                else
                {
                    minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
                    maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
                    minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
                    maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
                }
            }
            // fanouts of pRoot will be added to pRootNew
            if ( i == nSizeCore - 1 )
            {
                assert( pNodeAig == Abc_ObjRegular(pRootNew) );
                Abc_ObjForEachFanout( pRoot, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
                    // if ( !Abc_ObjIsTerm(pFanout) )
                    if ( pFanout->rawPos == 2 )
                    {
                        minXPosFanout = fminf(minXPosFanout, pFanout->xPosTmp);
                        maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPosTmp);
                        minYPosFanout = fminf(minYPosFanout, pFanout->yPosTmp);
                        maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPosTmp);
                    }
                    else
                    {
                        minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
                        maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
                        minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
                        maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
                    }
                }
            }
            if (minXPosFanout < 900000)
            {
                assert( nEndPoints + 1 < nEndPointCap );
                for ( k = 0; k < nEndPoints; k++ )
                    if ( xPosSet[k] >= minXPosFanout )
                        break;
                for ( l = nEndPoints; l > k; l-- )
                    xPosSet[l] = xPosSet[l - 1];
                xPosSet[k] = minXPosFanout;

                for ( ++k; k <= nEndPoints; k++ )
                    if ( xPosSet[k] >= maxXPosFanout )
                        break;
                for ( l = nEndPoints + 1; l > k; l-- )
                    xPosSet[l] = xPosSet[l - 1];
                xPosSet[k] = maxXPosFanout;

                for ( k = 0; k < nEndPoints; k++ )
                    if ( yPosSet[k] >= minYPosFanout )
                        break;
                for ( l = nEndPoints; l > k; l-- )
                    yPosSet[l] = yPosSet[l - 1];
                yPosSet[k] = minYPosFanout;

                for ( ++k; k <= nEndPoints; k++ )
                    if ( yPosSet[k] >= maxYPosFanout )
                        break;
                for ( l = nEndPoints + 1; l > k; l-- )
                    yPosSet[l] = yPosSet[l - 1];
                yPosSet[k] = maxYPosFanout;
                nEndPoints += 2;
            }
            if (nEndPoints >= 2)
            {
                pNodeAig->rectLx = xPosSet[nEndPoints / 2 - 1];
                pNodeAig->rectRx = xPosSet[nEndPoints / 2];
                pNodeAig->rectLy = yPosSet[nEndPoints / 2 - 1];
                pNodeAig->rectRy = yPosSet[nEndPoints / 2];
            }
            else
            {
                // mark floating nodes
                pNodeAig->rectLx = pNodeAig->rectRx = pNodeAig->rectLy = pNodeAig->rectRy = -10000.56;
            }
        }

        for ( int i = nSizeCore - 1; i >= 0; --i )
        {   
            pNodeAig = rawNodesCore[i];
            Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                if ( pFanout->rawPos == 1 )
                    break;
            if ( j == Abc_ObjFanoutNum(pNodeAig) )
            {
                // posXNew = (pNodeAig->rectLx + pNodeAig->rectRx) / 2;
                // posYNew = (pNodeAig->rectLy + pNodeAig->rectRy) / 2;

                // skip floating nodes
                if ( pNodeAig->rectLx == -10000.56 )
                {
                    printf("Idle subgraph!!!!\n");
                    assert ( pFanin->rectRx == -10000.56 );
                    assert ( pFanin->rectLy == -10000.56 );
                    assert ( pFanin->rectRy == -10000.56 );
                    pNodeAig->xPosTmp = 0;
                    pNodeAig->yPosTmp = 0;
                    continue;
                }
                else
                {
                    // find the best position
                    // fine-grained
                    if (pNodeAig->rectLx < pNtk->binOriX + pNodeAig->half_den_sizeX)
                        pNodeAig->rectLx = pNtk->binOriX + pNodeAig->half_den_sizeX;
                    if (pNodeAig->rectRx < pNtk->binOriX + pNodeAig->half_den_sizeX)
                        pNodeAig->rectRx = pNtk->binOriX + pNodeAig->half_den_sizeX;
                    if (pNodeAig->rectLy < pNtk->binOriY + pNodeAig->half_den_sizeY)
                        pNodeAig->rectLy = pNtk->binOriY + pNodeAig->half_den_sizeY;
                    if (pNodeAig->rectRy < pNtk->binOriY + pNodeAig->half_den_sizeY)
                        pNodeAig->rectRy = pNtk->binOriY + pNodeAig->half_den_sizeY;
                    if (pNodeAig->rectLx > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
                        pNodeAig->rectLx = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
                    if (pNodeAig->rectRx > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
                        pNodeAig->rectRx = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
                    if (pNodeAig->rectLy > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
                        pNodeAig->rectLy = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
                    if (pNodeAig->rectRy > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
                        pNodeAig->rectRy = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
                    xStep = (pNodeAig->rectRx - pNodeAig->rectLx) / 5;
                    yStep = (pNodeAig->rectRy - pNodeAig->rectLy) / 5;
                    if ( xStep < 0.05 )
                    {
                        xStep = 0.05;
                        xCnt = (int)((pNodeAig->rectRx - pNodeAig->rectLx) / 0.05);
                    }
                    else
                        xCnt = 5;
                    if ( yStep < 0.05 )
                    {
                        yStep = 0.05;
                        yCnt = (int)((pNodeAig->rectRy - pNodeAig->rectLy) / 0.05);
                    }
                    else
                        yCnt = 5;
                    xBest = 0;
                    yBest = 0;
                    minDensity = FLT_MAX;
                    minOverflow = FLT_MAX;
                    for (int ii = 0; ii <= xCnt; ++ii)
                        for (int jj = 0; jj <= yCnt; ++jj)
                        {
                            curDensity = 0;
                            curOverflow = 0;
                            xMin = pNodeAig->rectLx + ii * xStep - pNodeAig->half_den_sizeX;
                            yMin = pNodeAig->rectLy + jj * yStep - pNodeAig->half_den_sizeY;
                            xMax = pNodeAig->rectLx + ii * xStep + pNodeAig->half_den_sizeX;
                            yMax = pNodeAig->rectLy + jj * yStep + pNodeAig->half_den_sizeY;
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

                                xShare[xIdx - xb0] = x1 - x0;
                            }

                            for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
                                y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

                                y0 = fmaxf(y0, yMin);

                                y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

                                y1 = fminf(y1, yMax);

                                yShare[yIdx - yb0] = y1 - y0;
                            }

                            for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
                                for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                                    curDensity += pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] * xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNodeAig->den_scal;
                                    curOverflow += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] + xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal)
                                                    - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                                }
                            }

                            if ( curOverflow < minOverflow )
                            {
                                xBest = ii;
                                yBest = jj;
                                minOverflow = curOverflow;
                                minDensity = curDensity;
                            }
                            else if ( curOverflow == minOverflow && curDensity < minDensity )
                            {
                                xBest = ii;
                                yBest = jj;
                                minDensity = curDensity;
                            }
                            else if ( curOverflow == minOverflow && curDensity == minDensity 
                                && (fabsf(ii - xCnt / 2) * fabsf(ii - xCnt / 2) + fabsf(jj - yCnt / 2) * fabsf(jj - yCnt / 2) < fabsf(xBest - xCnt / 2) * fabsf(xBest - xCnt / 2) + fabsf(yBest - yCnt / 2) * fabsf(yBest - yCnt / 2)) )
                            {
                                xBest = ii;
                                yBest = jj;
                            }
                        }
                    assert( nNew < 3001 );
                    idxNewTmp[nNew++] = pNodeAig->Id; 
                    posXNew = pNodeAig->rectLx + xBest * xStep;
                    posYNew = pNodeAig->rectLy + yBest * yStep;
                    
                    xMin = posXNew - pNodeAig->half_den_sizeX;
                    yMin = posYNew - pNodeAig->half_den_sizeY;
                    xMax = posXNew + pNodeAig->half_den_sizeX;
                    yMax = posYNew + pNodeAig->half_den_sizeY;
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

                        xShare[xIdx - xb0] = x1 - x0;
                    }

                    for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
                        y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

                        y0 = fmaxf(y0, yMin);

                        y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

                        y1 = fminf(y1, yMax);

                        yShare[yIdx - yb0] = y1 - y0;
                    }

                    for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
                        for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                            pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
                            // printf("1. %d, %f\n", xIdx * pNtk->binDimY + yIdx, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                        }
                    }

                    posDiffCore += fabsf(posXNew - pNodeAig->xPosTmp) + fabsf(posYNew - pNodeAig->yPosTmp);
                    pNodeAig->xPosTmp = posXNew;
                    pNodeAig->yPosTmp = posYNew;
                    continue;
                }
            }
            minXPosFanout = 1000000;
            maxXPosFanout = -1000000;
            minYPosFanout = 1000000;
            maxYPosFanout = -1000000;
            nEndPoints = 0;
            Abc_ObjForEachFanin( pNodeAig, pFanin, j )
            {
                // IO floating mode: skip the terminals ****************************
                minXPosFanin = 1000000;
                maxXPosFanin = -1000000;
                minYPosFanin = 1000000;
                maxYPosFanin = -1000000;
                if ( !Abc_ObjIsTerm(pFanin) && pFanin->Id != 0 )
                {
                    if ( pFanin->rawPos == 1 )
                    {
                        // skip floating nodes
                        if ( pFanin->rectLx == -10000.56 )
                        {
                            assert ( pFanin->rectRx == -10000.56 );
                            assert ( pFanin->rectLy == -10000.56 );
                            assert ( pFanin->rectRy == -10000.56 );
                        }
                        else
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->rectLx);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->rectLx);
                            minXPosFanin = fminf(minXPosFanin, pFanin->rectRx);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->rectRx);
                            minYPosFanin = fminf(minYPosFanin, pFanin->rectLy);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->rectLy);
                            minYPosFanin = fminf(minYPosFanin, pFanin->rectRy);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->rectRy);
                        }
                    }
                    else
                    {
                        // if ( !Abc_ObjIsTerm(pFanin) )
                        if ( pFanin->rawPos == 2 )
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->xPosTmp);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPosTmp);
                            minYPosFanin = fminf(minYPosFanin, pFanin->yPosTmp);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPosTmp);
                        }
                        else
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->xPos);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPos);
                            minYPosFanin = fminf(minYPosFanin, pFanin->yPos);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPos);
                        }
                    }
                }

                Abc_ObjForEachFanout( pFanin, pFaninOut, k )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFaninOut) )
                        continue;
                    if ( pFaninOut->rawPos == 1 || Abc_NodeIsTravIdCurrent(pFaninOut) )
                        continue;
                    if (pFaninOut->Id == 0)
                        continue;
                    // assert( Abc_ObjIsTerm(pFaninOut) || pFaninOut->rawPos == 2 );
                    // if ( !Abc_ObjIsTerm(pFaninOut) )
                    if ( pFaninOut->rawPos == 2 )
                    {
                        minXPosFanin = fminf(minXPosFanin, pFaninOut->xPosTmp);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPosTmp);
                        minYPosFanin = fminf(minYPosFanin, pFaninOut->yPosTmp);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPosTmp);
                    }
                    else
                    {
                        minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
                        minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
                    }
                }

                if (minXPosFanin < 900000)
                {
                    assert( nEndPoints + 1 < nEndPointCap );
                    for ( k = 0; k < nEndPoints; k++ )
                        if ( xPosSet[k] >= minXPosFanin )
                            break;
                    for ( l = nEndPoints; l > k; l-- )
                        xPosSet[l] = xPosSet[l - 1];
                    xPosSet[k] = minXPosFanin;

                    for ( ++k; k <= nEndPoints; k++ )
                        if ( xPosSet[k] >= maxXPosFanin )
                            break;
                    for ( l = nEndPoints + 1; l > k; l-- )
                        xPosSet[l] = xPosSet[l - 1];
                    xPosSet[k] = maxXPosFanin;

                    for ( k = 0; k < nEndPoints; k++ )
                        if ( yPosSet[k] >= minYPosFanin )
                            break;
                    for ( l = nEndPoints; l > k; l-- )
                        yPosSet[l] = yPosSet[l - 1];
                    yPosSet[k] = minYPosFanin;

                    for ( ++k; k <= nEndPoints; k++ )
                        if ( yPosSet[k] >= maxYPosFanin )
                            break;
                    for ( l = nEndPoints + 1; l > k; l-- )
                        yPosSet[l] = yPosSet[l - 1];
                    yPosSet[k] = maxYPosFanin;
                    nEndPoints += 2;
                }
            }
            Abc_ObjForEachFanout( pNodeAig, pFanout, j )
            {   
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos );
                // if ( !Abc_ObjIsTerm(pFanout) )
                if ( pFanout->rawPos ) // core node or peripheral node
                {
                    minXPosFanout = fminf(minXPosFanout, pFanout->xPosTmp);
                    maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPosTmp);
                    minYPosFanout = fminf(minYPosFanout, pFanout->yPosTmp);
                    maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPosTmp);
                }
                else
                {
                    minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
                    maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
                    minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
                    maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
                }
            }

            if (minXPosFanout < 900000)
            {
                assert( nEndPoints + 1 < nEndPointCap );
                for ( k = 0; k < nEndPoints; k++ )
                    if ( xPosSet[k] >= minXPosFanout )
                        break;
                for ( l = nEndPoints; l > k; l-- )
                    xPosSet[l] = xPosSet[l - 1];
                xPosSet[k] = minXPosFanout;

                for ( ++k; k <= nEndPoints; k++ )
                    if ( xPosSet[k] >= maxXPosFanout )
                        break;
                for ( l = nEndPoints + 1; l > k; l-- )
                    xPosSet[l] = xPosSet[l - 1];
                xPosSet[k] = maxXPosFanout;

                for ( k = 0; k < nEndPoints; k++ )
                    if ( yPosSet[k] >= minYPosFanout )
                        break;
                for ( l = nEndPoints; l > k; l-- )
                    yPosSet[l] = yPosSet[l - 1];
                yPosSet[k] = minYPosFanout;

                for ( ++k; k <= nEndPoints; k++ )
                    if ( yPosSet[k] >= maxYPosFanout )
                        break;
                for ( l = nEndPoints + 1; l > k; l-- )
                    yPosSet[l] = yPosSet[l - 1];
                yPosSet[k] = maxYPosFanout;
                nEndPoints += 2;
	            }

	            // posXNew = (xPosSet[nEndPoints / 2 - 1] + xPosSet[nEndPoints / 2]) / 2;
	            // posYNew = (yPosSet[nEndPoints / 2 - 1] + yPosSet[nEndPoints / 2]) / 2;
	            assert( nEndPoints >= 2 );
	            assert( nEndPoints <= nEndPointCap );

	            // find the best position
	            // fine-grained
            if (xPosSet[nEndPoints / 2 - 1] < pNtk->binOriX + pNodeAig->half_den_sizeX)
                xPosSet[nEndPoints / 2 - 1] = pNtk->binOriX + pNodeAig->half_den_sizeX;
            if (xPosSet[nEndPoints / 2] < pNtk->binOriX + pNodeAig->half_den_sizeX)
                xPosSet[nEndPoints / 2] = pNtk->binOriX + pNodeAig->half_den_sizeX;
            if (yPosSet[nEndPoints / 2 - 1] < pNtk->binOriY + pNodeAig->half_den_sizeY)
                yPosSet[nEndPoints / 2 - 1] = pNtk->binOriY + pNodeAig->half_den_sizeY;
            if (yPosSet[nEndPoints / 2] < pNtk->binOriY + pNodeAig->half_den_sizeY)
                yPosSet[nEndPoints / 2] = pNtk->binOriY + pNodeAig->half_den_sizeY;
            if (xPosSet[nEndPoints / 2 - 1] > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
                xPosSet[nEndPoints / 2 - 1] = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
            if (xPosSet[nEndPoints / 2] > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
                xPosSet[nEndPoints / 2] = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
            if (yPosSet[nEndPoints / 2 - 1] > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
                yPosSet[nEndPoints / 2 - 1] = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
            if (yPosSet[nEndPoints / 2] > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
                yPosSet[nEndPoints / 2] = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
            xStep = (xPosSet[nEndPoints / 2] - xPosSet[nEndPoints / 2 - 1]) / 5;
            yStep = (yPosSet[nEndPoints / 2] - yPosSet[nEndPoints / 2 - 1]) / 5;
            if ( xStep < 0.05 )
            {
                xStep = 0.05;
                xCnt = (int)((xPosSet[nEndPoints / 2] - xPosSet[nEndPoints / 2 - 1]) / 0.05);
            }
            else
                xCnt = 5;
            if ( yStep < 0.05 )
            {
                yStep = 0.05;
                yCnt = (int)((yPosSet[nEndPoints / 2] - yPosSet[nEndPoints / 2 - 1]) / 0.05);
            }
            else
                yCnt = 5;
            xBest = 0;
            yBest = 0;
            minDensity = FLT_MAX;
            minOverflow = FLT_MAX;
            for (int ii = 0; ii <= xCnt; ++ii)
                for (int jj = 0; jj <= yCnt; ++jj)
                {
                    curDensity = 0;
                    curOverflow = 0;
                    xMin = xPosSet[nEndPoints / 2 - 1] + ii * xStep - pNodeAig->half_den_sizeX;
                    yMin = yPosSet[nEndPoints / 2 - 1] + jj * yStep - pNodeAig->half_den_sizeY;
                    xMax = xPosSet[nEndPoints / 2 - 1] + ii * xStep + pNodeAig->half_den_sizeX;
                    yMax = yPosSet[nEndPoints / 2 - 1] + jj * yStep + pNodeAig->half_den_sizeY;
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
                            curDensity += pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] * xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNodeAig->den_scal;
                            curOverflow += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] + xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal)
                                            - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                        }
                    }

                    if ( curOverflow < minOverflow )
                    {
                        xBest = ii;
                        yBest = jj;
                        minOverflow = curOverflow;
                        minDensity = curDensity;
                    }
                    else if ( curOverflow == minOverflow && curDensity < minDensity )
                    {
                        xBest = ii;
                        yBest = jj;
                        minDensity = curDensity;
                    }
                    else if ( curOverflow == minOverflow && curDensity == minDensity 
                        && (fabsf(ii - xCnt / 2) * fabsf(ii - xCnt / 2) + fabsf(jj - yCnt / 2) * fabsf(jj - yCnt / 2) < fabsf(xBest - xCnt / 2) * fabsf(xBest - xCnt / 2) + fabsf(yBest - yCnt / 2) * fabsf(yBest - yCnt / 2)) )
                    {
                        xBest = ii;
                        yBest = jj;
                    }
                }

            // density change
            assert( nNew < 3001 );
            idxNewTmp[nNew++] = pNodeAig->Id; 
            posXNew = xPosSet[nEndPoints / 2 - 1] + xBest * xStep;
            posYNew = yPosSet[nEndPoints / 2 - 1] + yBest * yStep;
            
            xMin = posXNew - pNodeAig->half_den_sizeX;
            yMin = posYNew - pNodeAig->half_den_sizeY;
            xMax = posXNew + pNodeAig->half_den_sizeX;
            yMax = posYNew + pNodeAig->half_den_sizeY;
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
                    pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
                    // printf("2. %d, %f\n", xIdx * pNtk->binDimY + yIdx, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                }
            }

            posDiffCore += fabsf(posXNew - pNodeAig->xPosTmp) + fabsf(posYNew - pNodeAig->yPosTmp);
            pNodeAig->xPosTmp = posXNew;
            pNodeAig->yPosTmp = posYNew;
        }

        // for ( int i = 0; i < nSizeCore; i++ )
        // {
        //     pNodeAig = rawNodesCore[i];
        //     // update the bounding box
        //     xGlobalMin = fminf(xGlobalMin, pNodeAig->xPosTmp);
        //     xGlobalMax = fmaxf(xGlobalMax, pNodeAig->xPosTmp);
        //     yGlobalMin = fminf(yGlobalMin, pNodeAig->yPosTmp);
        //     yGlobalMax = fmaxf(yGlobalMax, pNodeAig->yPosTmp);
        // }

        // early return, skip incremental placement ##############################
        if ( fSimple )
        {
            // node clustering: each node is assigned to its closet cluster F ~ m / dist^2
            if (fClustering)
                for ( i = 0; i < nSizeCore; ++i )
                {   
                    float maxForce = -1000, tmpForce;
                    int clusterId = -1;
                    pNodeAig = rawNodesCore[i];
                    for (j = 0; j < pNtk->nCellsPerCut; ++j)
                    {
                        tmpForce = Vec_IntEntry(&pNtk->vNodesPerCell, j) /
                            ((pNodeAig->xPosTmp - Vec_FltEntry(&pNtk->vXPosCell, j)) * (pNodeAig->xPosTmp - Vec_FltEntry(&pNtk->vXPosCell, j)) +
                             (pNodeAig->yPosTmp - Vec_FltEntry(&pNtk->vYPosCell, j)) * (pNodeAig->yPosTmp - Vec_FltEntry(&pNtk->vYPosCell, j)) + 0.001);
                        // tmpForce = pNtk->nNodesPerCell[j] * pNtk->nNodesPerCell[j] / ((pNodeAig->xPosTmp - pNtk->xPosCell[j])*(pNodeAig->xPosTmp - pNtk->xPosCell[j]) + (pNodeAig->yPosTmp - pNtk->yPosCell[j])*(pNodeAig->yPosTmp - pNtk->yPosCell[j]) + 0.001);
                        if (tmpForce > maxForce)
                        {
                            clusterId = j;
                            maxForce = tmpForce;
                        }
                    }
                    assert (clusterId != -1);
                    pNodeAig->xPosTmp = Vec_FltEntry(&pNtk->vXPosCell, clusterId);
                    pNodeAig->yPosTmp = Vec_FltEntry(&pNtk->vYPosCell, clusterId);
                }

            for ( i = 0; i < nSizeCore; ++i )
            {
                pNodeAig = rawNodesCore[i];
                Abc_ObjForEachFanin( pNodeAig, pFanin, j )
                    // if ( Abc_ObjIsTerm(pFanin) && !pFanin->rawPos )
                    if ( !pFanin->rawPos && !Abc_NodeIsTravIdCurrent(pFanin) )
                        {
                            if (pFanin->Id == 0)
                                continue;
                            assert( nSizeExtra < 3001 );
                            rawNodesExtra[nSizeExtra++] = pFanin;
                            pFanin->rawPos = 3;
                        }
            }
            // printf("nSizeExtra1 = %d\n", nSizeExtra);
            for ( i = 0; i < nSizePeri; ++i )
            {
                pNodeAig = rawNodesPeri[i];
                // Abc_ObjForEachFanin( pNodeAig, pFanin, j )
                //     printf("%d/%d/%d/%d & ", pNodeAig->Id, pFanin->Id, pFanin->rawPos, Abc_NodeIsTravIdCurrent(pFanin));
                Abc_ObjForEachFanin( pNodeAig, pFanin, j )
                    if ( !pFanin->rawPos && !Abc_NodeIsTravIdCurrent(pFanin) )
                        {
                            if (pFanin->Id == 0)
                                continue;
                            assert( nSizeExtra < 3001 );
                            rawNodesExtra[nSizeExtra++] = pFanin;
                            pFanin->rawPos = 3;
                            // printf("%d ** ", pFanin->Id);
                        }
            }
            // printf("nSizeExtra2 = %d\n", nSizeExtra);
            // printf("nSizePeri2 = %d\n", nSizePeri);
            for ( i = 0; i < nSizeExtra; ++i )
            {
                pNodeAig = rawNodesExtra[i];
                assert( pNodeAig->rawPos == 3 );
                pNodeAig->rawPos = 0;
            }
            // caculated local HPWL before and afer increamental placement
            for ( i = 0; i < nSizeCore; ++i )
            {
                pNodeAig = rawNodesCore[i];
                xMin = xMax = pNodeAig->xPos;
                yMin = yMax = pNodeAig->yPos;
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                    yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                    xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                    yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                }
                if ( i == nSizeCore - 1 )
                {
                    assert( pNodeAig == Abc_ObjRegular(pRootNew) );
                    Abc_ObjForEachFanout( pRoot, pFanout, j )
                    {
                        // IO floating mode: skip the terminals ****************************
                        if ( Abc_ObjIsTerm(pFanout) )
                            continue;
                        if (pFanout->Id == 0)
                            continue;
                        // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
                        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                    }
                }
                totalHPWL1 += xMax - xMin + yMax - yMin;

                xMin = xMax = pNodeAig->xPosTmp;
                yMin = yMax = pNodeAig->yPosTmp;
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
                    {
                        xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                        yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                        xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                        yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
                    }
                    else
                    {
                        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                    }
                }
                if ( i == nSizeCore - 1 )
                {
                    Abc_ObjForEachFanout( pRoot, pFanout, j )
                    {
                        // IO floating mode: skip the terminals ****************************
                        if ( Abc_ObjIsTerm(pFanout) )
                            continue;
                        if (pFanout->Id == 0)
                            continue;
                        if ( pFanout->rawPos == 2 )
                        {
                            xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                            yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                            xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                            yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
                        }
                        else
                        {
                            xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                            yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                            xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                            yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                        }
                    }
                }
                totalHPWL2 += xMax - xMin + yMax - yMin;
            }
            // printf("totalHPWL1 = %f\n", totalHPWL1);
            // printf("totalHPWL2 = %f\n", totalHPWL2);
            for ( i = 0; i < nSizePeri; ++i )
            {
                pNodeAig = rawNodesPeri[i];
                xMin = xMax = pNodeAig->xPos;
                yMin = yMax = pNodeAig->yPos;
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                    yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                    xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                    yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                }
                totalHPWL1 += xMax - xMin + yMax - yMin;

                xMin = xMax = pNodeAig->xPosTmp;
                yMin = yMax = pNodeAig->yPosTmp;
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
                    {
                        xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                        yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                        xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                        yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
                    }
                    else
                    {
                        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                    }
                }
                totalHPWL2 += xMax - xMin + yMax - yMin;
            }
            for ( i = 0; i < nSizeExtra; ++i )
            {
                pNodeAig = rawNodesExtra[i];
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pNodeAig) )
                {
                    xMin = 1000000;
                    yMin = 1000000;
                    xMax = -1000000;
                    yMax = -1000000;
                }
                else
                {
                    xMin = xMax = pNodeAig->xPos;
                    yMin = yMax = pNodeAig->yPos;
                }
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                    yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                    xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                    yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                }
                totalHPWL1 += xMax - xMin + yMax - yMin;

                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pNodeAig) )
                {
                    xMin = 1000000;
                    yMin = 1000000;
                    xMax = -1000000;
                    yMax = -1000000;
                }
                else
                {
                    xMin = xMax = pNodeAig->xPos;
                    yMin = yMax = pNodeAig->yPos;
                }
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
                    {
                        xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                        yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                        xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                        yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
                    }
                    else
                    {
                        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                    }
                }
                totalHPWL2 += xMax - xMin + yMax - yMin;
            }
            // printf("totalHPWL1 = %f\n", totalHPWL1);
            // printf("totalHPWL2 = %f\n", totalHPWL2);

            // // check the global HPWL before incremental placement
            // totalHPWL = 0;
            // Abc_NtkForEachObj(pNtk, pNodeAig, ii)
            // {
            //     if ( pNodeAig->rawPos == 0 && Abc_NodeIsTravIdCurrent(pNodeAig) )
            //         continue;
            //     // IO floating mode: skip the terminals ****************************
            //     if ( Abc_ObjIsTerm(pNodeAig) )
            //     {
            //         xMin = 1000000;
            //         yMin = 1000000;
            //         xMax = -1000000;
            //         yMax = -1000000;
            //     }
            //     else
            //     {
            //         xMin = xMax = pNodeAig->xPos;
            //         yMin = yMax = pNodeAig->yPos;
            //     }
            //     Abc_ObjForEachFanout( pNodeAig, pFanout, jj )
            //     {
            //         // IO floating mode: skip the terminals ****************************
            //         if ( Abc_ObjIsTerm(pFanout) )
            //             continue;
            //         if (pFanout->Id == 0)
            //             continue;
            //         if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
            //             continue;
            //         xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            //         yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            //         xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            //         yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            //     }
            //     if ( pNodeAig == Abc_ObjRegular(pRootNew) )
            //     {
            //         Abc_ObjForEachFanout( pRoot, pFanout, jj )
            //         {
            //             // IO floating mode: skip the terminals ****************************
            //             if ( Abc_ObjIsTerm(pFanout) )
            //                 continue;
            //             if (pFanout->Id == 0)
            //                 continue;
            //             // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
            //             xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            //             yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            //             xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            //             yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            //         }
            //     }
            //     if ( xMin == 1000000 )
            //         continue;
            //     totalHPWL += xMax - xMin + yMax - yMin;
            // }
            // printf("totalHPWLbefore = %f\n", totalHPWL);

            for ( i = 0; i < nSizeCore; ++i )
            {
                pNodeAig = rawNodesCore[i];
                pNodeAig->rawPos = 0;
                pNodeAig->xPos = pNodeAig->xPosTmp;
                pNodeAig->yPos = pNodeAig->yPosTmp;
            }
            for ( i = 0; i < nSizePeri; ++i )
            {
                pNodeAig = rawNodesPeri[i];
                pNodeAig->rawPos = 0;
                pNodeAig->xPos = pNodeAig->xPosTmp;
                pNodeAig->yPos = pNodeAig->yPosTmp;
            }

            // // check the global HPWL after incremental placement
            // totalHPWL = 0;
            // Abc_NtkForEachObj(pNtk, pNodeAig, ii)
            // {   
            //     // if (pNodeAig->Id == 0) continue;
            //     // assert ( pNodeAig->rawPos == 0 );
            //     if ( !Abc_ObjIsTerm(pNodeAig) && pNodeAig->rawPos == 0 && Abc_NodeIsTravIdCurrent(pNodeAig) )
            //         continue;
            //     // IO floating mode: skip the terminals ****************************
            //     if ( Abc_ObjIsTerm(pNodeAig) )
            //     {
            //         xMin = 1000000;
            //         yMin = 1000000;
            //         xMax = -1000000;
            //         yMax = -1000000;
            //     }
            //     else
            //     {
            //         xMin = xMax = pNodeAig->xPos;
            //         yMin = yMax = pNodeAig->yPos;
            //     }
            //     Abc_ObjForEachFanout( pNodeAig, pFanout, jj )
            //     {
            //         // IO floating mode: skip the terminals ****************************
            //         if ( Abc_ObjIsTerm(pFanout) )
            //             continue;
            //         if (pFanout->Id == 0)
            //             continue;
            //         if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
            //             continue;
            //         xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            //         yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            //         xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            //         yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            //     }
            //     if ( pNodeAig == Abc_ObjRegular(pRootNew) )
            //     {
            //         Abc_ObjForEachFanout( pRoot, pFanout, jj )
            //         {
            //             // IO floating mode: skip the terminals ****************************
            //             if ( Abc_ObjIsTerm(pFanout) )
            //                 continue;
            //             if (pFanout->Id == 0)
            //                 continue;
            //             // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
            //             xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            //             yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            //             xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            //             yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            //         }
            //     }
            //     if ( xMin == 1000000 )
            //         continue;
            //     totalHPWL += xMax - xMin + yMax - yMin;
            // }
            // printf("totalHPWLafter = %f\n", totalHPWL);

            return pRootNew;
        }


        // collect geometrically close nodes, which are in the bounding box
        int nSizePeriTmp = nSizePeri;
        int fflag;
        Abc_NtkForEachNode(pNtk, pNodeAig, i)
            if ( !Abc_ObjIsTerm(pNodeAig) && !pNodeAig->rawPos && !Abc_NodeIsTravIdCurrent(pNodeAig) )
                {
                    fflag = 0;
                    for ( int j = 0; j < nSizeCore; j++ )
                    {
                        Abc_Obj_t * pNodeTmp = rawNodesCore[j];
                        xGlobalMin = pNodeTmp->xPosTmp - 1 * pNtk->binStepX;
                        xGlobalMax = pNodeTmp->xPosTmp + 1 * pNtk->binStepX;
                        yGlobalMin = pNodeTmp->yPosTmp - 1 * pNtk->binStepY;
                        yGlobalMax = pNodeTmp->yPosTmp + 1 * pNtk->binStepY;
                        if (pNodeAig->xPos >= xGlobalMin && pNodeAig->xPos <= xGlobalMax && pNodeAig->yPos >= yGlobalMin && pNodeAig->yPos <= yGlobalMax)
                        {
                            assert( nSizePeri < nPeriCap );
                            rawNodesPeri[nSizePeri++] = pNodeAig;
                            pNodeAig->xPosTmp = pNodeAig->xPos;
                            pNodeAig->yPosTmp = pNodeAig->yPos;
                            pNodeAig->rawPos = 2;
                            fflag = 1;
                            break;
                        }
                    }
                    if ( !fflag )
                        for ( int j = 0; j < nSizePeriTmp; j++ )
                        {
                            Abc_Obj_t * pNodeTmp = rawNodesPeri[j];
                            xGlobalMin = pNodeTmp->xPosTmp - 0.5 * pNtk->binStepX;
                            xGlobalMax = pNodeTmp->xPosTmp + 0.5 * pNtk->binStepX;
                            yGlobalMin = pNodeTmp->yPosTmp - 0.5 * pNtk->binStepY;
                            yGlobalMax = pNodeTmp->yPosTmp + 0.5 * pNtk->binStepY;
                            if (pNodeAig->xPos >= xGlobalMin && pNodeAig->xPos <= xGlobalMax && pNodeAig->yPos >= yGlobalMin && pNodeAig->yPos <= yGlobalMax)
                            {
                                assert( nSizePeri < nPeriCap );
                                rawNodesPeri[nSizePeri++] = pNodeAig;
                                pNodeAig->xPosTmp = pNodeAig->xPos;
                                pNodeAig->yPosTmp = pNodeAig->yPos;
                                pNodeAig->rawPos = 2;
                                fflag = 1;
                                break;
                            }
                        }
                    if (nSizeCore + nSizePeri > 2000)
                        break;
                }
        // printf("nSizePeri2 = %d\n", nSizePeri);
        // printf("xGlobalMin = %f, xGlobalMax = %f, yGlobalMin = %f, yGlobalMax = %f\n", xGlobalMin, xGlobalMax, yGlobalMin, yGlobalMax);

    //     // Peripheral nodes
    //     for ( int i = 0; i < nSizePeri; i++ )
    //     {
    //         minXPosFanout = 1000000;
    //         maxXPosFanout = -1000000;
    //         minYPosFanout = 1000000;
    //         maxYPosFanout = -1000000;
    //         nEndPoints = 0;
    //         pNodeAig = rawNodesPeri[i];

    //         // density change
    //         idxOldTmp[nOld++] = pNodeAig->Id; 
    //         xMin = pNodeAig->xPos - pNodeAig->half_den_sizeX;
    //         yMin = pNodeAig->yPos - pNodeAig->half_den_sizeY;
    //         xMax = pNodeAig->xPos + pNodeAig->half_den_sizeX;
    //         yMax = pNodeAig->yPos + pNodeAig->half_den_sizeY;
    //         xb0 = (int)((xMin - pNtk->binOriX) / pNtk->binStepX);
    //         yb0 = (int)((yMin - pNtk->binOriY) / pNtk->binStepY);
    //         xb1 = (int)((xMax - pNtk->binOriX) / pNtk->binStepX);
    //         yb1 = (int)((yMax - pNtk->binOriY) / pNtk->binStepY);
    //         if (xb0 < 0)
    //             xb0 = 0;
    //         if (yb0 < 0)
    //             yb0 = 0;
    //         if (xb1 >= pNtk->binDimX)
    //             xb1 = pNtk->binDimX - 1;
    //         if (yb1 >= pNtk->binDimY)
    //             yb1 = pNtk->binDimY - 1;
            
    //         for( int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //             x0 = xIdx * pNtk->binStepX + pNtk->binOriX;

    //             x0 = fmaxf(x0, xMin);

    //             x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;

    //             x1 = fminf(x1, xMax);

    //             xShare[xIdx - xb0] = x1 - x0;
    //         }

    //         for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //             y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

    //             y0 = fmaxf(y0, yMin);

    //             y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

    //             y1 = fminf(y1, yMax);

    //             yShare[yIdx - yb0] = y1 - y0;
    //         }
    //         for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //             for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //                 pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] -= xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
    //             }
    //         }

    //         Abc_ObjForEachFanin( pNodeAig, pFanin, j )
    //         {
    //             if (pFanin->Id == 0)
    //                 continue;
    //             // replace the old pRoot with pRootNew
    //             if ( pFanin == pRoot )
    //                 pFanin = Abc_ObjRegular(pRootNew);
    //             minXPosFanin = 1000000;
    //             maxXPosFanin = -1000000;
    //             minYPosFanin = 1000000;
    //             maxYPosFanin = -1000000;
    //             if ( pFanin->rawPos )
    //             {
    //                 minXPosFanin = fminf(minXPosFanin, pFanin->xPosTmp);
    //                 maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPosTmp);
    //                 minYPosFanin = fminf(minYPosFanin, pFanin->yPosTmp);
    //                 maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPosTmp);
    //             }
    //             else
    //             {
    //                 minXPosFanin = fminf(minXPosFanin, pFanin->xPos);
    //                 maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPos);
    //                 minYPosFanin = fminf(minYPosFanin, pFanin->yPos);
    //                 maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPos);
    //             }
    //             Abc_ObjForEachFanout( pFanin, pFaninOut, k )
    //             {
    //                 if ( pFaninOut == pNodeAig || (pFaninOut->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFaninOut)) )
    //                     continue;
    //                 if (pFaninOut->Id == 0)
    //                     continue;
    //                 if ( pFaninOut->rawPos )
    //                 {
    //                     minXPosFanin = fminf(minXPosFanin, pFaninOut->xPosTmp);
    //                     maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPosTmp);
    //                     minYPosFanin = fminf(minYPosFanin, pFaninOut->yPosTmp);
    //                     maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPosTmp);
    //                 }
    //                 else
    //                 {
    //                     minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
    //                     maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
    //                     minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
    //                     maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
    //                 }
    //             }
    //             if ( pFanin == Abc_ObjRegular(pRootNew) )
    //             {
    //                 // printf("\n%d, %d, %d\n", pNodeAig->Id, Abc_ObjFanoutNum(pFanin), Abc_ObjFanoutNum(pRoot));
    //                 Abc_ObjForEachFanout( pRoot, pFaninOut, k )
    //                 {
    //                     assert( Abc_ObjIsTerm(pFaninOut) || pFaninOut->rawPos == 2 );
    //                     if ( pFaninOut == pNodeAig )
    //                         continue;
    //                     if (pFaninOut->Id == 0)
    //                         continue;
    //                     if ( !Abc_ObjIsTerm(pFaninOut) )
    //                     {
    //                         minXPosFanin = fminf(minXPosFanin, pFaninOut->xPosTmp);
    //                         maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPosTmp);
    //                         minYPosFanin = fminf(minYPosFanin, pFaninOut->yPosTmp);
    //                         maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPosTmp);
    //                     }
    //                     else
    //                     {
    //                         minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
    //                         maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
    //                         minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
    //                         maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
    //                     }
    //                 }
    //             }

    //             assert(nEndPoints <= 100);
    //             for ( k = 0; k < nEndPoints; k++ )
    //                 if ( xPosSet[k] >= minXPosFanin )
    //                     break;
    //             for ( l = nEndPoints; l > k; l-- )
    //                 xPosSet[l] = xPosSet[l - 1];
    //             xPosSet[k] = minXPosFanin;

    //             for ( ++k; k <= nEndPoints; k++ )
    //                 if ( xPosSet[k] >= maxXPosFanin )
    //                     break;
    //             for ( l = nEndPoints + 1; l > k; l-- )
    //                 xPosSet[l] = xPosSet[l - 1];
    //             xPosSet[k] = maxXPosFanin;

    //             for ( k = 0; k < nEndPoints; k++ )
    //                 if ( yPosSet[k] >= minYPosFanin )
    //                     break;
    //             for ( l = nEndPoints; l > k; l-- )
    //                 yPosSet[l] = yPosSet[l - 1];
    //             yPosSet[k] = minYPosFanin;

    //             for ( ++k; k <= nEndPoints; k++ )
    //                 if ( yPosSet[k] >= maxYPosFanin )
    //                     break;
    //             for ( l = nEndPoints + 1; l > k; l-- )
    //                 yPosSet[l] = yPosSet[l - 1];
    //             yPosSet[k] = maxYPosFanin;
    //             nEndPoints += 2;
    //         }
    //         Abc_ObjForEachFanout( pNodeAig, pFanout, j )
    //         {
    //             if ( (pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout)) )
    //                 continue;
    //             if (pFanout->Id == 0)
    //                 continue;
    //             if ( pFanout->rawPos )
    //             {
    //                 minXPosFanout = fminf(minXPosFanout, pFanout->xPosTmp);
    //                 maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPosTmp);
    //                 minYPosFanout = fminf(minYPosFanout, pFanout->yPosTmp);
    //                 maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPosTmp);
    //             }
    //             else
    //             {
    //                 minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
    //                 maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
    //                 minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
    //                 maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
    //             }
    //         }
    //         if (minXPosFanout < 900000)
    //         {
    //             assert(nEndPoints <= 100);
    //             for ( k = 0; k < nEndPoints; k++ )
    //                 if ( xPosSet[k] >= minXPosFanout )
    //                     break;
    //             for ( l = nEndPoints; l > k; l-- )
    //                 xPosSet[l] = xPosSet[l - 1];
    //             xPosSet[k] = minXPosFanout;

    //             for ( ++k; k <= nEndPoints; k++ )
    //                 if ( xPosSet[k] >= maxXPosFanout )
    //                     break;
    //             for ( l = nEndPoints + 1; l > k; l-- )
    //                 xPosSet[l] = xPosSet[l - 1];
    //             xPosSet[k] = maxXPosFanout;

    //             for ( k = 0; k < nEndPoints; k++ )
    //                 if ( yPosSet[k] >= minYPosFanout )
    //                     break;
    //             for ( l = nEndPoints; l > k; l-- )
    //                 yPosSet[l] = yPosSet[l - 1];
    //             yPosSet[k] = minYPosFanout;

    //             for ( ++k; k <= nEndPoints; k++ )
    //                 if ( yPosSet[k] >= maxYPosFanout )
    //                     break;
    //             for ( l = nEndPoints + 1; l > k; l-- )
    //                 yPosSet[l] = yPosSet[l - 1];
    //             yPosSet[k] = maxYPosFanout;
    //             nEndPoints += 2;
    //         }

    //         // posXNew = (xPosSet[nEndPoints / 2 - 1] + xPosSet[nEndPoints / 2]) / 2;
    //         // posYNew = (yPosSet[nEndPoints / 2 - 1] + yPosSet[nEndPoints / 2]) / 2;

    //         // find the best position
    //         // fine-grained
    //         if (xPosSet[nEndPoints / 2 - 1] < pNtk->binOriX + pNodeAig->half_den_sizeX)
    //             xPosSet[nEndPoints / 2 - 1] = pNtk->binOriX + pNodeAig->half_den_sizeX;
    //         if (xPosSet[nEndPoints / 2] < pNtk->binOriX + pNodeAig->half_den_sizeX)
    //             xPosSet[nEndPoints / 2] = pNtk->binOriX + pNodeAig->half_den_sizeX;
    //         if (yPosSet[nEndPoints / 2 - 1] < pNtk->binOriY + pNodeAig->half_den_sizeY)
    //             yPosSet[nEndPoints / 2 - 1] = pNtk->binOriY + pNodeAig->half_den_sizeY;
    //         if (yPosSet[nEndPoints / 2] < pNtk->binOriY + pNodeAig->half_den_sizeY)
    //             yPosSet[nEndPoints / 2] = pNtk->binOriY + pNodeAig->half_den_sizeY;
    //         if (xPosSet[nEndPoints / 2 - 1] > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
    //             xPosSet[nEndPoints / 2 - 1] = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
    //         if (xPosSet[nEndPoints / 2] > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
    //             xPosSet[nEndPoints / 2] = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
    //         if (yPosSet[nEndPoints / 2 - 1] > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
    //             yPosSet[nEndPoints / 2 - 1] = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
    //         if (yPosSet[nEndPoints / 2] > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
    //             yPosSet[nEndPoints / 2] = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
    //         xStep = (xPosSet[nEndPoints / 2] - xPosSet[nEndPoints / 2 - 1]) / 5;
    //         yStep = (yPosSet[nEndPoints / 2] - yPosSet[nEndPoints / 2 - 1]) / 5;
    //         if ( xStep < 0.05 )
    //         {
    //             xStep = 0.05;
    //             xCnt = (int)((xPosSet[nEndPoints / 2] - xPosSet[nEndPoints / 2 - 1]) / 0.05);
    //         }
    //         else
    //             xCnt = 5;
    //         if ( yStep < 0.05 )
    //         {
    //             yStep = 0.05;
    //             yCnt = (int)((yPosSet[nEndPoints / 2] - yPosSet[nEndPoints / 2 - 1]) / 0.05);
    //         }
    //         else
    //             yCnt = 5;
    //         minDensity = 1000;
    //         minOverflow = 1000;
    //         for (int ii = 0; ii <= xCnt; ++ii)
    //             for (int jj = 0; jj <= yCnt; ++jj)
    //             {
    //                 curDensity = 0;
    //                 curOverflow = 0;
    //                 xMin = xPosSet[nEndPoints / 2 - 1] + ii * xStep - pNodeAig->half_den_sizeX;
    //                 yMin = yPosSet[nEndPoints / 2 - 1] + jj * yStep - pNodeAig->half_den_sizeY;
    //                 xMax = xPosSet[nEndPoints / 2 - 1] + ii * xStep + pNodeAig->half_den_sizeX;
    //                 yMax = yPosSet[nEndPoints / 2 - 1] + jj * yStep + pNodeAig->half_den_sizeY;
    //                 xb0 = (int)((xMin - pNtk->binOriX) / pNtk->binStepX);
    //                 yb0 = (int)((yMin - pNtk->binOriY) / pNtk->binStepY);
    //                 xb1 = (int)((xMax - pNtk->binOriX) / pNtk->binStepX);
    //                 yb1 = (int)((yMax - pNtk->binOriY) / pNtk->binStepY);
    //                 if (xb0 < 0)
    //                     xb0 = 0;
    //                 if (yb0 < 0)
    //                     yb0 = 0;
    //                 if (xb1 >= pNtk->binDimX)
    //                     xb1 = pNtk->binDimX - 1;
    //                 if (yb1 >= pNtk->binDimY)
    //                     yb1 = pNtk->binDimY - 1;
                    
    //                 for( int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //                     x0 = xIdx * pNtk->binStepX + pNtk->binOriX;

    //                     x0 = fmaxf(x0, xMin);

    //                     x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;

    //                     x1 = fminf(x1, xMax);

    //                     xShare[xIdx - xb0] = x1 - x0;
    //                 }

    //                 for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //                     y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

    //                     y0 = fmaxf(y0, yMin);

    //                     y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

    //                     y1 = fminf(y1, yMax);

    //                     yShare[yIdx - yb0] = y1 - y0;
    //                 }

    //                 for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //                     for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //                         curDensity += pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] * xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNodeAig->den_scal;
    //                         curOverflow += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] + xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal)
    //                                         - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
    //                     }
    //                 }

    //                 if ( curOverflow < minOverflow )
    //                 {
    //                     xBest = ii;
    //                     yBest = jj;
    //                     minOverflow = curOverflow;
    //                     minDensity = curDensity;
    //                 }
    //                 else if ( curOverflow == minOverflow && curDensity < minDensity )
    //                 {
    //                     xBest = ii;
    //                     yBest = jj;
    //                     minDensity = curDensity;
    //                 }
    //                 else if ( curOverflow == minOverflow && curDensity == minDensity 
    //                     && (fabsf(ii - xCnt / 2) * fabsf(ii - xCnt / 2) + fabsf(jj - yCnt / 2) * fabsf(jj - yCnt / 2) < fabsf(xBest - xCnt / 2) * fabsf(xBest - xCnt / 2) + fabsf(yBest - yCnt / 2) * fabsf(yBest - yCnt / 2)) )
    //                 {
    //                     xBest = ii;
    //                     yBest = jj;
    //                 }
    //             }

    //         // density change
    //         idxNewTmp[nNew++] = pNodeAig->Id; 
    //         posXNew = xPosSet[nEndPoints / 2 - 1] + xBest * xStep;
    //         posYNew = yPosSet[nEndPoints / 2 - 1] + yBest * yStep;
            
    //         xMin = posXNew - pNodeAig->half_den_sizeX;
    //         yMin = posYNew - pNodeAig->half_den_sizeY;
    //         xMax = posXNew + pNodeAig->half_den_sizeX;
    //         yMax = posYNew + pNodeAig->half_den_sizeY;
    //         xb0 = (int)((xMin - pNtk->binOriX) / pNtk->binStepX);
    //         yb0 = (int)((yMin - pNtk->binOriY) / pNtk->binStepY);
    //         xb1 = (int)((xMax - pNtk->binOriX) / pNtk->binStepX);
    //         yb1 = (int)((yMax - pNtk->binOriY) / pNtk->binStepY);
    //         if (xb0 < 0)
    //             xb0 = 0;
    //         if (yb0 < 0)
    //             yb0 = 0;
    //         if (xb1 >= pNtk->binDimX)
    //             xb1 = pNtk->binDimX - 1;
    //         if (yb1 >= pNtk->binDimY)
    //             yb1 = pNtk->binDimY - 1;
            
    //         for( int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //             x0 = xIdx * pNtk->binStepX + pNtk->binOriX;

    //             x0 = fmaxf(x0, xMin);

    //             x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;

    //             x1 = fminf(x1, xMax);

    //             xShare[xIdx - xb0] = x1 - x0;
    //         }

    //         for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //             y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

    //             y0 = fmaxf(y0, yMin);

    //             y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

    //             y1 = fminf(y1, yMax);

    //             yShare[yIdx - yb0] = y1 - y0;
    //         }

    //         for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //             for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //                 pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
    //                 // printf("3. %d, %f\n", xIdx * pNtk->binDimY + yIdx, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
    //             }
    //         }

    //         posDiffPeri += fabsf(posXNew - pNodeAig->xPosTmp) + fabsf(posYNew - pNodeAig->yPosTmp);
    //         pNodeAig->xPosTmp = posXNew;
    //         pNodeAig->yPosTmp = posYNew;
    //         // printf("%d, %d & %.2f, %.2f & %.2f ** ", pNodeAig->Id, Abc_ObjFanoutNum(pNodeAig), pNodeAig->xPosTmp, pNodeAig->yPosTmp, posDiffPeri);
    //     }
        posDiffCore /= nSizeCore;
        posDiffPeri /= nSizePeri;
        // printf("Iter = %d\n", iter);
        // printf("posDiffCore = %f\n", posDiffCore);
        // printf("posDiffPeri = %f\n", posDiffPeri);
    }

    for ( i = 0; i < nSizeCore; ++i )
    {
        pNodeAig = rawNodesCore[i];
        Abc_ObjForEachFanin( pNodeAig, pFanin, j )
            if ( Abc_ObjIsTerm(pFanin) && !pFanin->rawPos )
                {
                    if (pFanin->Id == 0)
                        continue;
                    assert( nSizeExtra < 3001 );
                    rawNodesExtra[nSizeExtra++] = pFanin;
                    pFanin->rawPos = 3;
                }
    }
    // printf("nSizeExtra1 = %d\n", nSizeExtra);
    for ( i = 0; i < nSizePeri; ++i )
    {
        pNodeAig = rawNodesPeri[i];
        // Abc_ObjForEachFanin( pNodeAig, pFanin, j )
        //     printf("%d/%d/%d/%d & ", pNodeAig->Id, pFanin->Id, pFanin->rawPos, Abc_NodeIsTravIdCurrent(pFanin));
        Abc_ObjForEachFanin( pNodeAig, pFanin, j )
            if ( !pFanin->rawPos && !Abc_NodeIsTravIdCurrent(pFanin) )
                {
                    if (pFanin->Id == 0)
                        continue;
                    assert( nSizeExtra < 3001 );
                    rawNodesExtra[nSizeExtra++] = pFanin;
                    pFanin->rawPos = 3;
                    // printf("%d ** ", pFanin->Id);
                }
    }
    // printf("nSizeExtra2 = %d\n", nSizeExtra);
    // printf("nSizePeri2 = %d\n", nSizePeri);
    for ( i = 0; i < nSizeExtra; ++i )
    {
        pNodeAig = rawNodesExtra[i];
        assert( pNodeAig->rawPos == 3 );
        pNodeAig->rawPos = 0;
    }
    // printf("Iter = %d\n", iter);
    // printf("posDiffCore = %f\n", posDiffCore);
    // printf("posDiffPeri = %f\n", posDiffPeri);


    // incremental placement with simple placer ###################################
    float grad_x[3001], grad_y[3001];
    float grad_x1[3001], grad_y1[3001];
    float alpha = 10.0;
    float step_size = 0.3;
    float overflow = 0;
    int nCoveredBins = 0;
    assert(nSizeCore + nSizePeri < 3001);

    for (int iter = 0; iter < 50; iter++)
    {
        overflow = 0.0;
        nCoveredBins = 0;
        memset(grad_x, 0, (nSizeCore + nSizePeri) * sizeof(float));
        memset(grad_y, 0, (nSizeCore + nSizePeri) * sizeof(float));
        memset(grad_x1, 0, (nSizeCore + nSizePeri) * sizeof(float));
        memset(grad_y1, 0, (nSizeCore + nSizePeri) * sizeof(float));
        for ( int id = 0; id < nSizeCore + nSizePeri; ++id )
        {
            if (id < nSizeCore)
                pNodeAig = rawNodesCore[id];
            else 
                pNodeAig = rawNodesPeri[id - nSizeCore];

            // wireLength gradient
            // pNodeAig as a sink
            xMin = xMax = pNodeAig->xPosTmp;
            yMin = yMax = pNodeAig->yPosTmp;
            Abc_ObjForEachFanin( pNodeAig, pFanin, i) {
                // IO floating mode: skip the terminals ****************************
                if ( !Abc_ObjIsTerm(pFanin) && pFanin->Id != 0 )
                {
                    xMin = fminf(xMin, pFanin->xPosTmp);
                    xMax = fmaxf(xMax, pFanin->xPosTmp);
                    yMin = fminf(yMin, pFanin->yPosTmp);
                    yMax = fmaxf(yMax, pFanin->yPosTmp);
                }
                Abc_ObjForEachFanout(pFanin, pFaninOut, j) {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFaninOut) )
                        continue;
                    if (pFaninOut->Id == 0)
                        continue;
                    xMin = fminf(xMin, pFaninOut->xPosTmp);
                    xMax = fmaxf(xMax, pFaninOut->xPosTmp);
                    yMin = fminf(yMin, pFaninOut->yPosTmp);
                    yMax = fmaxf(yMax, pFaninOut->yPosTmp);
                }

                // X direction gradient
                if (fabsf(pNodeAig->xPosTmp - xMin) < 1e-9) {
                    // pNodeAig is at the left boundary (min_x)
                    grad_x[id] -= 1.0;  // Moving right decreases HPWL
                } else if (fabsf(pNodeAig->xPosTmp - xMax) < 1e-9) {
                    // pNodeAig is at the right boundary (max_x)
                    grad_x[id] += 1.0;  // Moving right increases HPWL
                }
                // If pNodeAig is in the middle, gradient is 0
                
                // Y direction gradient
                if (fabsf(pNodeAig->yPosTmp - yMin) < 1e-9) {
                    // pNodeAig is at the bottom boundary (min_y)
                    grad_y[id] -= 1.0;  // Moving up decreases HPWL
                } else if (fabsf(pNodeAig->yPosTmp - yMax) < 1e-9) {
                    // pNodeAig is at the top boundary (max_y)
                    grad_y[id] += 1.0;  // Moving up increases HPWL
                }
                // If pNodeAig is in the middle, gradient is 0
            }

            // pNodeAig as a driver
            xMin = xMax = pNodeAig->xPosTmp;
            yMin = yMax = pNodeAig->yPosTmp;
            Abc_ObjForEachFanout( pNodeAig, pFanout, i) {
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                xMin = fminf(xMin, pFanout->xPosTmp);
                xMax = fmaxf(xMax, pFanout->xPosTmp);
                yMin = fminf(yMin, pFanout->yPosTmp);
                yMax = fmaxf(yMax, pFanout->yPosTmp);

                // X direction gradient
                if (fabsf(pNodeAig->xPosTmp - xMin) < 1e-9) {
                    // pNodeAig is at the left boundary (min_x)
                    grad_x[id] -= 1.0;  // Moving right decreases HPWL
                } else if (fabsf(pNodeAig->xPosTmp - xMax) < 1e-9) {
                    // pNodeAig is at the right boundary (max_x)
                    grad_x[id] += 1.0;  // Moving right increases HPWL
                }
                // If pNodeAig is in the middle, gradient is 0
                
                // Y direction gradient
                if (fabsf(pNodeAig->yPosTmp - yMin) < 1e-9) {
                    // pNodeAig is at the bottom boundary (min_y)
                    grad_y[id] -= 1.0;  // Moving up decreases HPWL
                } else if (fabsf(pNodeAig->yPosTmp - yMax) < 1e-9) {
                    // pNodeAig is at the top boundary (max_y)
                    grad_y[id] += 1.0;  // Moving up increases HPWL
                }
                // If pNodeAig is in the middle, gradient is 0
            }

            // density gradient
            int bin_xMin = (int)((pNodeAig->xPosTmp - pNodeAig->half_den_sizeX - pNtk->binOriX) / pNtk->binStepX);
            int bin_yMin = (int)((pNodeAig->yPosTmp - pNodeAig->half_den_sizeY - pNtk->binOriY) / pNtk->binStepY);
            int bin_xMax = (int)((pNodeAig->xPosTmp + pNodeAig->half_den_sizeX - pNtk->binOriX) / pNtk->binStepX);
            int bin_yMax = (int)((pNodeAig->yPosTmp + pNodeAig->half_den_sizeY - pNtk->binOriY) / pNtk->binStepY);
            int bottom_idx, top_idx;
            int left_idx, right_idx;
            float overflow_bottom, overflow_top, overflow_left, overflow_right, overflow_avg = 0.0;

            assert (bin_xMax - bin_xMin >= 0 && bin_xMax - bin_xMin < 10);
            assert (bin_yMax - bin_yMin >= 0 && bin_yMax - bin_yMin < 10);
            if ( bin_xMax == pNtk->binDimX )
                bin_xMax--;
            if ( bin_yMax == pNtk->binDimY )
                bin_yMax--;
            
            // Calculate average overflow
            for( int xIdx = bin_xMin; xIdx <= bin_xMax; xIdx++) {
                x0 = xIdx * pNtk->binStepX + pNtk->binOriX;
                x0 = fmaxf(x0, pNodeAig->xPosTmp - pNodeAig->half_den_sizeX);
                x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;
                x1 = fminf(x1, pNodeAig->xPosTmp + pNodeAig->half_den_sizeX);
                xShare[xIdx - bin_xMin] = x1 - x0;
            }

            for( int yIdx = bin_yMin; yIdx <= bin_yMax; yIdx++) {
                y0 = yIdx * pNtk->binStepY + pNtk->binOriY;
                y0 = fmaxf(y0, pNodeAig->yPosTmp - pNodeAig->half_den_sizeY);
                y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;
                y1 = fminf(y1, pNodeAig->yPosTmp + pNodeAig->half_den_sizeY);
                yShare[yIdx - bin_yMin] = y1 - y0;
            }

            for(int xIdx = bin_xMin; xIdx <= bin_xMax; xIdx++) {
                for(int yIdx = bin_yMin; yIdx <= bin_yMax; yIdx++) {
                    overflow_avg += fmaxf(0.0, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] - 1.0) * xShare[xIdx - bin_xMin] * yShare[yIdx - bin_yMin];
                }
            }
            overflow_avg /= 4 * pNodeAig->half_den_sizeX * pNodeAig->half_den_sizeY;

            // Calculate gradient using central difference in neighboring bins
            for (int i = bin_yMin; i <= bin_yMax; ++i) {
                bottom_idx = bin_xMin * pNtk->binDimY + i;
                top_idx = bin_xMax * pNtk->binDimY + i;
                overflow_bottom = fmaxf(0.0, pNtk->binsDensity[bottom_idx] - 1.0);
                overflow_top = fmaxf(0.0, pNtk->binsDensity[top_idx] - 1.0);

                grad_x1[id] += (overflow_top - overflow_bottom + (pNtk->binsDensity[top_idx] - pNtk->binsDensity[bottom_idx]) * overflow_avg) * pNodeAig->den_scal;
            }

            for (int i = bin_xMin; i <= bin_xMax; ++i) {
                left_idx = i * pNtk->binDimY + bin_yMin;
                right_idx = i * pNtk->binDimY + bin_yMax;
                overflow_left = fmaxf(0.0, pNtk->binsDensity[left_idx] - 1.0);
                overflow_right = fmaxf(0.0, pNtk->binsDensity[right_idx] - 1.0);

                grad_y1[id] += (overflow_right - overflow_left + (pNtk->binsDensity[right_idx] - pNtk->binsDensity[left_idx]) * overflow_avg) * pNodeAig->den_scal;
            }

            grad_x[id] += alpha * grad_x1[id];
            grad_y[id] += alpha * grad_y1[id];
            float force_mag = sqrt(grad_x[id] * grad_x[id] + grad_y[id] * grad_y[id]);
            float max_force = fminf(pNtk->binStepX * pNtk->binDimX, pNtk->binStepY * pNtk->binDimY) / 100.0;
            if (force_mag > max_force) {
                grad_x[id] *= max_force / force_mag;
                grad_y[id] *= max_force / force_mag;
            }
        }
        for ( int id = 0; id < nSizeCore + nSizePeri; ++id )
        {
            if (id < nSizeCore)
                pNodeAig = rawNodesCore[id];
            else 
                pNodeAig = rawNodesPeri[id - nSizeCore];

            // density change after removing changed nodes
            xMin = pNodeAig->xPosTmp - pNodeAig->half_den_sizeX;
            yMin = pNodeAig->yPosTmp - pNodeAig->half_den_sizeY;
            xMax = pNodeAig->xPosTmp + pNodeAig->half_den_sizeX;
            yMax = pNodeAig->yPosTmp + pNodeAig->half_den_sizeY;
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
                    pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] -= xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
                    // printf("3. %d, %f\n", xIdx * pNtk->binDimY + yIdx, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                }
            }
            // Update position
            pNodeAig->xPosTmp -= step_size * grad_x[id];
            pNodeAig->yPosTmp -= step_size * grad_y[id];
            // STRICT boundary enforcement
            float margin_x = pNodeAig->half_den_sizeX; // Extra margin
            float margin_y = pNodeAig->half_den_sizeY;
            pNodeAig->xPosTmp = fmaxf(pNtk->binOriX + margin_x, 
                            fminf(pNtk->binOriX + pNtk->binStepX * pNtk->binDimX - margin_x, pNodeAig->xPosTmp));
            pNodeAig->yPosTmp = fmaxf(pNtk->binOriY + margin_y, 
                            fminf(pNtk->binOriY + pNtk->binStepY * pNtk->binDimY - margin_y, pNodeAig->yPosTmp));
            // density change after placing changed nodes
            xMin = pNodeAig->xPosTmp - pNodeAig->half_den_sizeX;
            yMin = pNodeAig->yPosTmp - pNodeAig->half_den_sizeY;
            xMax = pNodeAig->xPosTmp + pNodeAig->half_den_sizeX;
            yMax = pNodeAig->yPosTmp + pNodeAig->half_den_sizeY;
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
                    pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
                    // printf("3. %d, %f\n", xIdx * pNtk->binDimY + yIdx, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                    overflow += fmaxf(0.0, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] - 1.0);
                    nCoveredBins++;
                }
            }
        }
        // float overflow = 0.0;
        // for (int i = 0; i < pNtk->nBins; i++) {
        //     overflow += fmaxf(0.0, pNtk->binsDensity[i] - 1.0);
        // }
        overflow *= pNtk->binModuleRatio * pNtk->nBins / (float)nCoveredBins;
        if (overflow > 0.15) {
            alpha *= 1.5;  // More aggressive than before
            // std::cout << "  Density violation! Increased alpha to " << alpha << std::endl;
        } 
        else if (overflow > 0.1) {
            alpha *= 1.2;
            step_size *= 0.9;
        }
        else {
            alpha *= 0.8; // Reduce penalty when density is good
            step_size *= 0.8;
            if (step_size < 0.01)
                break;
        }
        // printf("%d, %f, %f, %f\n", iter, overflow, alpha, step_size);
    }
    // printf("%d, %f, %f, %f\n", iter, overflow, alpha, step_size);
    // end incremental placement with simple placer ###################################

    // caculated local HPWL before and afer increamental placement
    for ( i = 0; i < nSizeCore; ++i )
    {
        pNodeAig = rawNodesCore[i];
        xMin = xMax = pNodeAig->xPos;
        yMin = yMax = pNodeAig->yPos;
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
        }
        if ( i == nSizeCore - 1 )
        {
            assert( pNodeAig == Abc_ObjRegular(pRootNew) );
            Abc_ObjForEachFanout( pRoot, pFanout, j )
            {
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
        }
        totalHPWL1 += xMax - xMin + yMax - yMin;

        xMin = xMax = pNodeAig->xPosTmp;
        yMin = yMax = pNodeAig->yPosTmp;
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
            {
                xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
            }
            else
            {
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
        }
        if ( i == nSizeCore - 1 )
        {
            Abc_ObjForEachFanout( pRoot, pFanout, j )
            {
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                if ( pFanout->rawPos == 2 )
                {
                    xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                    yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                    xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                    yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
                }
                else
                {
                    xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                    yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                    xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                    yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                }
            }
        }
        totalHPWL2 += xMax - xMin + yMax - yMin;
    }
    // printf("totalHPWL1 = %f\n", totalHPWL1);
    // printf("totalHPWL2 = %f\n", totalHPWL2);
    for ( i = 0; i < nSizePeri; ++i )
    {
        pNodeAig = rawNodesPeri[i];
        xMin = xMax = pNodeAig->xPos;
        yMin = yMax = pNodeAig->yPos;
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
        }
        totalHPWL1 += xMax - xMin + yMax - yMin;

        xMin = xMax = pNodeAig->xPosTmp;
        yMin = yMax = pNodeAig->yPosTmp;
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
            {
                xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
            }
            else
            {
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
        }
        totalHPWL2 += xMax - xMin + yMax - yMin;
    }
    // printf("totalHPWL1 = %f\n", totalHPWL1);
    // printf("totalHPWL2 = %f\n", totalHPWL2);
    for ( i = 0; i < nSizeExtra; ++i )
    {
        pNodeAig = rawNodesExtra[i];
        // IO floating mode: skip the terminals ****************************
        if ( Abc_ObjIsTerm(pNodeAig) )
        {
            xMin = 1000000;
            yMin = 1000000;
            xMax = -1000000;
            yMax = -1000000;
        }
        else
        {
            xMin = xMax = pNodeAig->xPos;
            yMin = yMax = pNodeAig->yPos;
        }
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
        }
        totalHPWL1 += xMax - xMin + yMax - yMin;

        // IO floating mode: skip the terminals ****************************
        if ( Abc_ObjIsTerm(pNodeAig) )
        {
            xMin = 1000000;
            yMin = 1000000;
            xMax = -1000000;
            yMax = -1000000;
        }
        else
        {
            xMin = xMax = pNodeAig->xPos;
            yMin = yMax = pNodeAig->yPos;
        }
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
            {
                xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
            }
            else
            {
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
        }
        totalHPWL2 += xMax - xMin + yMax - yMin;
    }
    // printf("totalHPWL1 = %f\n", totalHPWL1);
    // printf("totalHPWL2 = %f\n", totalHPWL2);

    for ( i = 0; i < nSizeCore; ++i )
    {
        pNodeAig = rawNodesCore[i];
        pNodeAig->rawPos = 0;
        pNodeAig->xPos = pNodeAig->xPosTmp;
        pNodeAig->yPos = pNodeAig->yPosTmp;
    }
    for ( i = 0; i < nSizePeri; ++i )
    {
        pNodeAig = rawNodesPeri[i];
        pNodeAig->rawPos = 0;
        pNodeAig->xPos = pNodeAig->xPosTmp;
        pNodeAig->yPos = pNodeAig->yPosTmp;
    }

// float overflow = 0;
//         for (int i = 0; i < pNtk->nBins; ++i)
//         {
//             // printf("%.2f && ", pNtk->binsDensity[i]);
//             overflow += (pNtk->binsDensity[i] > 1) ? pNtk->binsDensity[i] - 1 : 0;
//         }
//         overflow *= pNtk->binModuleRatio;
        // printf("\nOverflow111111 = %f\n", overflow);

    // printf("!!Final: HPWLAdded = %.2f\n", increHPWL1 + increHPWL2 + totalHPWL2 - totalHPWL1);
    return pRootNew;
}
/**Function*************************************************************

  Synopsis    [Transforms the decomposition graph into the AIG.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Abc_Obj_t * Dec_SopToAig( Abc_Ntk_t * pNtk, char * pSop, Vec_Ptr_t * vFaninAigs )
{
    Abc_Obj_t * pFunc;
    Dec_Graph_t * pFForm;
    Dec_Node_t * pNode;
    int i;
    pFForm = Dec_Factor( pSop );
    Dec_GraphForEachLeaf( pFForm, pNode, i )
        pNode->pFunc = Vec_PtrEntry( vFaninAigs, i );
    pFunc = Dec_GraphToNetwork( pNtk, pFForm );
    Dec_GraphFree( pFForm );
    return pFunc;
}

/**Function*************************************************************

  Synopsis    [Transforms the decomposition graph into the AIG.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Abc_Obj_t * Dec_GraphToAig( Abc_Ntk_t * pNtk, Dec_Graph_t * pFForm, Vec_Ptr_t * vFaninAigs )
{
    Abc_Obj_t * pFunc;
    Dec_Node_t * pNode;
    int i;
    Dec_GraphForEachLeaf( pFForm, pNode, i )
        pNode->pFunc = Vec_PtrEntry( vFaninAigs, i );
    pFunc = Dec_GraphToNetwork( pNtk, pFForm );
    return pFunc;
}

/**Function*************************************************************

  Synopsis    [Transforms the decomposition graph into the AIG.]

  Description [AIG nodes for the fanins should be assigned to pNode->pFunc
  of the leaves of the graph before calling this procedure.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Abc_Obj_t * Dec_GraphToNetworkNoStrash( Abc_Ntk_t * pNtk, Dec_Graph_t * pGraph )
{
    Abc_Obj_t * pAnd, * pAnd0, * pAnd1;
    Dec_Node_t * pNode = NULL; // Suppress "might be used uninitialized"
    int i;
    // check for constant function
    if ( Dec_GraphIsConst(pGraph) )
        return Abc_ObjNotCond( Abc_AigConst1(pNtk), Dec_GraphIsComplement(pGraph) );
    // check for a literal
    if ( Dec_GraphIsVar(pGraph) )
        return Abc_ObjNotCond( (Abc_Obj_t *)Dec_GraphVar(pGraph)->pFunc, Dec_GraphIsComplement(pGraph) );
    // build the AIG nodes corresponding to the AND gates of the graph
    Dec_GraphForEachNode( pGraph, pNode, i )
    {
        pAnd0 = Abc_ObjNotCond( (Abc_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge0.Node)->pFunc, pNode->eEdge0.fCompl ); 
        pAnd1 = Abc_ObjNotCond( (Abc_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge1.Node)->pFunc, pNode->eEdge1.fCompl ); 
//        pNode->pFunc = Abc_AigAnd( (Abc_Aig_t *)pNtk->pManFunc, pAnd0, pAnd1 );
        pAnd = Abc_NtkCreateNode( pNtk );
        Abc_ObjAddFanin( pAnd, pAnd0 );
        Abc_ObjAddFanin( pAnd, pAnd1 );
        pNode->pFunc = pAnd;
    }
    // complement the result if necessary
    return Abc_ObjNotCond( (Abc_Obj_t *)pNode->pFunc, Dec_GraphIsComplement(pGraph) );
}

/**Function*************************************************************

  Synopsis    [Counts the number of new nodes added when using this graph.]

  Description [AIG nodes for the fanins should be assigned to pNode->pFunc 
  of the leaves of the graph before calling this procedure. 
  Returns -1 if the number of nodes and levels exceeded the given limit or 
  the number of levels exceeded the maximum allowed level.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Dec_GraphToNetworkCount( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int NodeMax, int LevelMax )
{
    Abc_Aig_t * pMan = (Abc_Aig_t *)pRoot->pNtk->pManFunc;
    Dec_Node_t * pNode, * pNode0, * pNode1;
    Abc_Obj_t * pAnd, * pAnd0, * pAnd1;
    int i, Counter, LevelNew, LevelOld;
    // check for constant function or a literal
    if ( Dec_GraphIsConst(pGraph) || Dec_GraphIsVar(pGraph) )
        return 0;
    // set the levels of the leaves
    Dec_GraphForEachLeaf( pGraph, pNode, i )
        pNode->Level = Abc_ObjRegular((Abc_Obj_t *)pNode->pFunc)->Level;
    // compute the AIG size after adding the internal nodes
    Counter = 0;
    Dec_GraphForEachNode( pGraph, pNode, i )
    {
        // get the children of this node
        pNode0 = Dec_GraphNode( pGraph, pNode->eEdge0.Node );
        pNode1 = Dec_GraphNode( pGraph, pNode->eEdge1.Node );
        // get the AIG nodes corresponding to the children 
        pAnd0 = (Abc_Obj_t *)pNode0->pFunc; 
        pAnd1 = (Abc_Obj_t *)pNode1->pFunc; 
        if ( pAnd0 && pAnd1 )
        {
            // if they are both present, find the resulting node
            pAnd0 = Abc_ObjNotCond( pAnd0, pNode->eEdge0.fCompl );
            pAnd1 = Abc_ObjNotCond( pAnd1, pNode->eEdge1.fCompl );
            pAnd  = Abc_AigAndLookup( pMan, pAnd0, pAnd1 );
            // return -1 if the node is the same as the original root
            if ( Abc_ObjRegular(pAnd) == pRoot )
                return -1;
        }
        else
            pAnd = NULL;
        // count the number of added nodes
        if ( pAnd == NULL || Abc_NodeIsTravIdCurrent(Abc_ObjRegular(pAnd)) )
        {
            // ++Counter;
            if ( ++Counter > NodeMax )
                return -1;
        }
        // count the number of new levels
        LevelNew = 1 + Abc_MaxInt( pNode0->Level, pNode1->Level );
        if ( pAnd )
        {
            if ( Abc_ObjRegular(pAnd) == Abc_AigConst1(pRoot->pNtk) )
                LevelNew = 0;
            else if ( Abc_ObjRegular(pAnd) == Abc_ObjRegular(pAnd0) )
                LevelNew = (int)Abc_ObjRegular(pAnd0)->Level;
            else if ( Abc_ObjRegular(pAnd) == Abc_ObjRegular(pAnd1) )
                LevelNew = (int)Abc_ObjRegular(pAnd1)->Level;
            LevelOld = (int)Abc_ObjRegular(pAnd)->Level;
//            assert( LevelNew == LevelOld );
        }
        if ( LevelNew > LevelMax )
            return -1;
        pNode->pFunc = pAnd;
        pNode->Level = LevelNew;
    }
    return Counter;
}


float Dec_GraphToNetworkHPWL( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int LevelMax, float overflow, float overflowBefore, float overflowAfter, int fSimple, int fClustering,
    int * pRootArrivalT, int * pRootReqT, float * pRootArrivalG, float * pRootReqG )
{
    Abc_Ntk_t * pNtk = pRoot->pNtk;
    Abc_Aig_t * pMan = (Abc_Aig_t *)pNtk->pManFunc;
    Dec_Node_t * pNode, * pNode0, * pNode1;
    Abc_Obj_t * pAnd, * pAnd0, * pAnd1;
    Abc_Obj_t * pAnd0R, * pAnd1R, * pNodeR;
    Abc_Obj_t * pFanout;
    Abc_Obj_t * pRootNew;
    
    float xMin, xMax, yMin, yMax;
    int added = 0;
    int Counter, LevelNew, LevelOld;
    int i, j;
    float increHPWL1 = 0.0;
    int nNodesNew, nNodesOld, RetValue;
    float increHPWL2 = 0.0, newHPWL = 0.0, oldHPWL = 0.0;

    int nPeriCap = 64;
    Abc_Obj_t * rawNodesCore[3001], * rawNodesPeri[64], * rawNodesExtra[3001];
    Abc_Obj_t * pNodeAig;
    Abc_Obj_t * pFanin, * pFaninOut;
    int k, l, iter = 0, nSizeCore = 0, nSizePeri = 0, nSizeExtra = 0;
    float minXPosFanin, maxXPosFanin, minYPosFanin, maxYPosFanin, minXPosFanout, maxXPosFanout, minYPosFanout, maxYPosFanout;
    float posDiffCore = 10, posDiffPeri = 10, posDiffCorePre = 1000;
    float posXNew, posYNew;
    int nEndPointCap = 10;
    float xPosSet[nEndPointCap], yPosSet[nEndPointCap];
    // float rectLx[100000], rectRx[100000], rectLy[100000], rectRy[100000];
    int nEndPoints;

    int xIdx, yIdx;
    float xOff, yOff;
    int xIdx1, yIdx1;
    float xOff1, yOff1;
    float xOldTmp[3001], yOldTmp[3001], xNewTmp[3001], yNewTmp[3001];
    int idxOldTmp[3001], idxNewTmp[3001];
    int nOld = 0, nNew = 0;
    float minDensity, curDensity;
    int xBest, yBest;
    float xStep, yStep;
    float minOverflow, curOverflow;
    int xCnt, yCnt;
    float halfBinRatio = pNtk->halfBinRatio;
    int nShareCap = 10;

    // float xMin, yMin, xMax, yMax;
    int xb0, xb1, yb0, yb1;
    float x0, x1, y0, y1;
    float xShare[nShareCap], yShare[nShareCap];
    float scaleX, scaleY;
    float xGlobalMin = 100000, xGlobalMax = -100000, yGlobalMin = 100000, yGlobalMax = -100000;

    float totalHPWL1 = 0, totalHPWL2 = 0;
    float HPWLResult;
    int nTimingSaves = 0;
    Dec_TimingSave_t TimingSaves[DEC_TIMING_SAVE_MAX];

    if ( pRootArrivalT )
        *pRootArrivalT = pRoot->arrivalT;
    if ( pRootReqT )
        *pRootReqT = pRoot->reqT;
    if ( pRootArrivalG )
        *pRootArrivalG = pRoot->arrivalG;
    if ( pRootReqG )
        *pRootReqG = pRoot->reqG;

    // check for constant function or a literal
    if ( Dec_GraphIsConst(pGraph) || Dec_GraphIsVar(pGraph) )
        return 0;
    // set the levels of the leaves
    Dec_GraphForEachLeaf( pGraph, pNode, i )
        pNode->Level = Abc_ObjRegular((Abc_Obj_t *)pNode->pFunc)->Level;
    // examine level and see if there's any new node added
    Counter = 0;
    Dec_GraphForEachNode( pGraph, pNode, i )
    {
        // get the children of this node
        pNode0 = Dec_GraphNode( pGraph, pNode->eEdge0.Node );
        pNode1 = Dec_GraphNode( pGraph, pNode->eEdge1.Node );
        // get the AIG nodes corresponding to the children 
        pAnd0 = (Abc_Obj_t *)pNode0->pFunc; 
        pAnd1 = (Abc_Obj_t *)pNode1->pFunc; 
        if ( pAnd0 && pAnd1 )
        {
            // if they are both present, find the resulting node
            pAnd0 = Abc_ObjNotCond( pAnd0, pNode->eEdge0.fCompl );
            pAnd1 = Abc_ObjNotCond( pAnd1, pNode->eEdge1.fCompl );
            pAnd  = Abc_AigAndLookup( pMan, pAnd0, pAnd1 );
            // return -1 if the node is the same as the original root
            if ( Abc_ObjRegular(pAnd) == pRoot )
                return -1;
        }
        else
            pAnd = NULL;
        // count the number of added nodes
        if ( pAnd == NULL || Abc_NodeIsTravIdCurrent(Abc_ObjRegular(pAnd)) )
            ++Counter;
        if ( pAnd == NULL )
            added = 1;
        // count the number of new levels
        LevelNew = 1 + Abc_MaxInt( pNode0->Level, pNode1->Level );
        if ( pAnd )
        {
            if ( Abc_ObjRegular(pAnd) == Abc_AigConst1(pRoot->pNtk) )
                LevelNew = 0;
            else if ( Abc_ObjRegular(pAnd) == Abc_ObjRegular(pAnd0) )
                LevelNew = (int)Abc_ObjRegular(pAnd0)->Level;
            else if ( Abc_ObjRegular(pAnd) == Abc_ObjRegular(pAnd1) )
                LevelNew = (int)Abc_ObjRegular(pAnd1)->Level;
            LevelOld = (int)Abc_ObjRegular(pAnd)->Level;
//            assert( LevelNew == LevelOld );
        }
        if ( LevelNew > LevelMax )
            return -1;
        pNode->pFunc = pAnd;
        pNode->Level = LevelNew;
    }
    // calculate the added internal HPWL and collect rawNodesCore
    // Dec_GraphForEachLeaf( pGraph, pNode, i )
    //     printf("%d, ", Abc_ObjRegular((Abc_Obj_t *)pNode->pFunc)->Id);
    // printf("\n");
    Dec_GraphForEachNode( pGraph, pNode, i )
    {
        pAnd0 = Abc_ObjNotCond( (Abc_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge0.Node)->pFunc, pNode->eEdge0.fCompl ); 
        pAnd1 = Abc_ObjNotCond( (Abc_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge1.Node)->pFunc, pNode->eEdge1.fCompl ); 
        pNode->pFunc = Abc_AigAnd( pMan, pAnd0, pAnd1 );

        pNodeR = Abc_ObjRegular((Abc_Obj_t *)pNode->pFunc);
        pAnd0R = Abc_ObjRegular(pAnd0);
        pAnd1R = Abc_ObjRegular(pAnd1);
        // printf("%d, %d\n", pAnd0R->Id, pAnd1R->Id);

        // check if the node is already in the rawNodesCore set
        int flag = 0;
        for (int k = 0; k < nSizeCore; k++)
            if ( rawNodesCore[k]->Id == pNodeR->Id )
            {
                flag = 1;
                break;
            }
        if (flag) continue;

        if ( pNodeR->rawPos )
        {
            pNodeR->xPos = ( pAnd0R->xPos + pAnd1R->xPos + pRoot->xPos ) / 3;
            pNodeR->yPos = ( pAnd0R->yPos + pAnd1R->yPos + pRoot->yPos ) / 3;
        }
        if ( pNodeR->rawPos || Abc_NodeIsTravIdCurrent(pNodeR) )
        {
            // HPWL increase after adding the edge between pAnd0 and pNode
            // if pAnd0R is a Ci, ignore the wire
            if ( Abc_ObjIsTerm(pAnd0R) )
            {
                xMin = 1000000;
                yMin = 1000000;
                xMax = -1000000;
                yMax = -1000000;
            }
            else
            {
                xMin = xMax = pAnd0R->xPos;
                yMin = yMax = pAnd0R->yPos;
            }
            Abc_ObjForEachFanout( pAnd0R, pFanout, j)
            {
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if ( pFanout == pNodeR )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                    continue;
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
            if ( pNodeR->xPos < xMin )
                increHPWL1 += xMin - pNodeR->xPos;
            if ( pNodeR->xPos > xMax )
                increHPWL1 += pNodeR->xPos - xMax;
            if ( pNodeR->yPos < yMin )
                increHPWL1 += yMin - pNodeR->yPos;
            if ( pNodeR->yPos > yMax )
                increHPWL1 += pNodeR->yPos - yMax;
            // HPWL increase after adding the edge between pAnd1 and pNode
            // if pAnd1R is a Ci, ignore the wire
            if ( Abc_ObjIsTerm(pAnd1R) )
            {
                xMin = 1000000;
                yMin = 1000000;
                xMax = -1000000;
                yMax = -1000000;
            }
            else
            {
                xMin = xMax = pAnd1R->xPos;
                yMin = yMax = pAnd1R->yPos;
            }
            Abc_ObjForEachFanout( pAnd1R, pFanout, j)
            {
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if ( pFanout == pNodeR )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                    continue;
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
            if ( pNodeR->xPos < xMin )
                increHPWL1 += xMin - pNodeR->xPos;
            if ( pNodeR->xPos > xMax )
                increHPWL1 += pNodeR->xPos - xMax;
            if ( pNodeR->yPos < yMin )
                increHPWL1 += yMin - pNodeR->yPos;
            if ( pNodeR->yPos > yMax )
                increHPWL1 += pNodeR->yPos - yMax;
            
            // the size of the added node
            pNodeR->half_den_sizeX = (Abc_ObjFaninC0(pNodeR) && Abc_ObjFaninC1(pNodeR)) ? 1 : 1.5;
            pNodeR->half_den_sizeY = 0.5;
            // SQRT2 = smoothing parameter for density_size calculation
            if(pNodeR->half_den_sizeX < pNtk->binStepX / 2 * 1.414213562373095048801) {
                scaleX = pNodeR->half_den_sizeX / (pNtk->binStepX / 2 * 1.414213562373095048801);
                pNodeR->half_den_sizeX = pNtk->binStepX / 2 * 1.414213562373095048801;
            }
                else {
                scaleX = 1.0;
            }

            if(pNodeR->half_den_sizeY < pNtk->binStepY / 2 * 1.414213562373095048801) {
                scaleY = pNodeR->half_den_sizeY / (pNtk->binStepY / 2 * 1.414213562373095048801);
                pNodeR->half_den_sizeY = pNtk->binStepY / 2 * 1.414213562373095048801;
            }
                else {
                scaleY = 1.0;
            }
            pNodeR->den_scal = scaleX * scaleY;
        }
        // density change
        else
        {
            assert( nOld < 3001 );
            idxOldTmp[nOld++] = pNodeR->Id; 
            xMin = pNodeR->xPos - pNodeR->half_den_sizeX;
            yMin = pNodeR->yPos - pNodeR->half_den_sizeY;
            xMax = pNodeR->xPos + pNodeR->half_den_sizeX;
            yMax = pNodeR->yPos + pNodeR->half_den_sizeY;
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
                    overflow += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] - xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeR->den_scal)
                            - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                    pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] -= xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeR->den_scal;
                }
            }
        }
        pNodeR->rawPos = 1;
        assert( nSizeCore < 3001 );
        rawNodesCore[nSizeCore++] = pNodeR;
        pNodeR->xPosTmp = pNodeR->xPos;
        pNodeR->yPosTmp = pNodeR->yPos;
        // // update the bounding box
        // xGlobalMin = fminf(xGlobalMin, pNodeR->xPos);
        // xGlobalMax = fmaxf(xGlobalMax, pNodeR->xPos);
        // yGlobalMin = fminf(yGlobalMin, pNodeR->yPos);
        // yGlobalMax = fmaxf(yGlobalMax, pNodeR->yPos);
        // printf("%d, %.2f, %.2f\n", pNodeR->Id, pNodeR->xPos, pNodeR->yPos);
    }
    // printf("increHPWL1 = %.2f\n", increHPWL1);

    // calculate the added external HPWL (between the root and its fanouts)
    pRootNew = Abc_ObjNotCond( (Abc_Obj_t *)pNode->pFunc, Dec_GraphIsComplement(pGraph) );
    xMin = xMax = Abc_ObjRegular(pRootNew)->xPos;
    yMin = yMax = Abc_ObjRegular(pRootNew)->yPos;
    Abc_ObjForEachFanout( Abc_ObjRegular(pRootNew), pFanout, i )
    {   
        // IO floating mode: skip the terminals ***************************
        if ( Abc_ObjIsTerm(pFanout) )
            continue;
        if (pFanout->Id == 0)
            continue;
        if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
            continue;
        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
    }
    oldHPWL = xMax - xMin + yMax - yMin;
    Abc_ObjForEachFanout( Abc_ObjRegular(pRoot), pFanout, i )
    {
        // IO floating mode: skip the terminals ****************************
        if ( Abc_ObjIsTerm(pFanout) )
            continue;
        if (pFanout->Id == 0)
            continue;
        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
    }
    newHPWL = xMax - xMin + yMax - yMin;
    increHPWL2 = newHPWL - oldHPWL;
    // printf("newHPWL = %.2f\n", newHPWL);
    // printf("oldHPWL = %.2f\n", oldHPWL);
    // printf("increHPWL2 = %.2f\n", increHPWL2);

    // printf("Counter = %d\n", Counter);
    // printf("pRootNew->rawPos = %d\n", Abc_ObjRegular(pRootNew)->rawPos);

    // // expand the core to a maximal tree
    // // Abc_Obj_t * addedCoreNodes[10000];
    // int headIdx = 0, tailIdx = 0;
    // int nFanouts = 0;
    // // for ( i = 0; i < nSizeCore; ++i )
    // // {
    // //     addedCoreNodes[tailIdx++] = rawNodesCore[i];
    // // }
    // tailIdx = nSizeCore;
    // while ( headIdx < tailIdx && tailIdx < 10 * nSizeCore )
    // {
    //     pNodeAig = rawNodesCore[headIdx++];
    //     Abc_ObjForEachFanin( pNodeAig, pFanin, j )
    //     {
    //         if ( !Abc_ObjIsTerm(pFanin) && !pFanin->rawPos && !Abc_NodeIsTravIdCurrent(pFanin) )
    //         {
    //             if (pFanin->Id == 0)
    //                 continue;
    //             nFanouts = 0;
    //             Abc_ObjForEachFanout( pFanin, pFaninOut, k )
    //                 if ( pFaninOut->rawPos == 1 )
    //                     nFanouts++;
    //             if ( nFanouts == 1 )
    //             {
    //                 pFanin->rawPos = 1;
    //                 rawNodesCore[tailIdx++] = pFanin;
    //                 // addedCoreNodes[tailIdx++] = pFanin;
    //                 pFanin->xPosTmp = pFanin->xPos;
    //                 pFanin->yPosTmp = pFanin->yPos;
    //                 // printf("*******%d\n", pFanin->Id);
    //             }
    //         }
    //     }
    // }
    // // reorder the rawNodes
    // for ( i = tailIdx; i < 2 * tailIdx; ++i)
    //     rawNodesCore[i] = rawNodesCore[i - tailIdx];
    // for ( i = 0; i < tailIdx - nSizeCore; ++i )
    //     rawNodesCore[i] = rawNodesCore[2 * tailIdx - i - 1];
    // for ( i = tailIdx - nSizeCore; i < tailIdx; ++i )
    //     rawNodesCore[i] = rawNodesCore[i + nSizeCore];
    // nSizeCore = tailIdx;

    // collect rawNodesPeri which are fanins and their fanouts, and fanouts of rawNodesCore
    // for ( i = 0; i < nSizeCore; ++i )
    // {
    //     pNodeAig = rawNodesCore[i];
    //     // printf("%d & ", pNodeAig->Id);
    //     Abc_ObjForEachFanin( pNodeAig, pFanin, j )
    //     {
    //         if ( !Abc_ObjIsTerm(pFanin) && !pFanin->rawPos && !Abc_NodeIsTravIdCurrent(pFanin) )
    //         {
    //             if (pFanin->Id == 0)
    //                 continue;
    //             rawNodesPeri[nSizePeri++] = pFanin;
    //             pFanin->rawPos = 2;
    //             // printf("%d &&& ", pFanin->Id);
    //         }
    //         Abc_ObjForEachFanout( pFanin, pFaninOut, k )
    //         {
    //             // printf("%d ***** ", pFaninOut->Id);
    //             if ( !Abc_ObjIsTerm(pFaninOut) && !pFaninOut->rawPos && !Abc_NodeIsTravIdCurrent(pFaninOut) )
    //             {
    //                 if (pFaninOut->Id == 0)
    //                     continue;
    //                 // rawNodesPeri[nSizePeri++] = pFaninOut;
    //                 // pFaninOut->rawPos = 2;
    //                 // copy the positions of the fanouts of the fanin
    //                 pFaninOut->xPosTmp = pFaninOut->xPos;
    //                 pFaninOut->yPosTmp = pFaninOut->yPos;
    //                 // printf("%d &&&&& ", pFaninOut->Id);
    //             }
    //         }
    //     }
    //     Abc_ObjForEachFanout( pNodeAig, pFanout, j )
    //         if ( !Abc_ObjIsTerm(pFanout) && !pFanout->rawPos && !Abc_NodeIsTravIdCurrent(pFanout) )
    //         {
    //             if (pFanout->Id == 0)
    //                 continue;
    //             rawNodesPeri[nSizePeri++] = pFanout;
    //             pFanout->rawPos = 2;
    //             // printf("%d *** ", pFaninOut->Id);
    //         }
    //     // printf("\n");
    // }
    // Abc_ObjForEachFanout( pRoot, pFanout, i )     // fanouts of pRoot will be added to pRootNew
    //     if ( !Abc_ObjIsTerm(pFanout) && !pFanout->rawPos )
    //     {
    //         if (pFanout->Id == 0)
    //             continue;
    //         rawNodesPeri[nSizePeri++] = pFanout;
    //         pFanout->rawPos = 2;
    //         // printf("%d^^^\n", pFanout->Id);
    //     }
    // assert ( nSizePeri == 0 );
    for ( i = 0; i < nSizePeri; ++i )
    {
        pNodeAig = rawNodesPeri[i];
        pNodeAig->xPosTmp = pNodeAig->xPos;
        pNodeAig->yPosTmp = pNodeAig->yPos;
        // // update the bounding box
        // xGlobalMin = fminf(xGlobalMin, pNodeAig->xPos);
        // xGlobalMax = fmaxf(xGlobalMax, pNodeAig->xPos);
        // yGlobalMin = fminf(yGlobalMin, pNodeAig->yPos);
        // yGlobalMax = fmaxf(yGlobalMax, pNodeAig->yPos);
        // printf("%d ** ", pNodeAig->Id);
    }
    // printf("nSizeCore = %d\n", nSizeCore);
    // printf("nSizePeri1 = %d\n", nSizePeri);

    // ########## incremental placement ##########
    // while ( posDiffCore > pow(10.0, -2) && fabsf(posDiffCore - posDiffCorePre) > pow(10.0, -3) )
    {   
        posDiffCorePre = posDiffCore;
        posDiffCore = 0;
        posDiffPeri = 0;
        iter++;
        // Core nodes
        // for ( int i = 0; i < nSizeCore; i++ )
        // {
        //     pNodeAig = rawNodesCore[i];
        //     idxOldTmp[nOld++] = pNodeAig->Id;
        //     idxNewTmp[nNew++] = pNodeAig->Id;
        // }
        for ( int i = 0; i < nSizeCore; ++i )
        {   
            minXPosFanout = 1000000;
            maxXPosFanout = -1000000;
            minYPosFanout = 1000000;
            maxYPosFanout = -1000000;
            nEndPoints = 0;
            pNodeAig = rawNodesCore[i];
            Abc_ObjForEachFanin( pNodeAig, pFanin, j )
            {
                // IO floating mode: skip the terminals ****************************
                minXPosFanin = 1000000;
                maxXPosFanin = -1000000;
                minYPosFanin = 1000000;
                maxYPosFanin = -1000000;
                if ( !Abc_ObjIsTerm(pFanin) && pFanin->Id != 0 )
                {
                    if ( pFanin->rawPos == 1 )
                    {
                        // skip floating nodes
                        if ( pFanin->rectLx == -10000.56 )
                        {
                            assert ( pFanin->rectRx == -10000.56 );
                            assert ( pFanin->rectLy == -10000.56 );
                            assert ( pFanin->rectRy == -10000.56 );
                        }
                        else
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->rectLx);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->rectLx);
                            minXPosFanin = fminf(minXPosFanin, pFanin->rectRx);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->rectRx);
                            minYPosFanin = fminf(minYPosFanin, pFanin->rectLy);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->rectLy);
                            minYPosFanin = fminf(minYPosFanin, pFanin->rectRy);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->rectRy);
                        }
                    }
                    else
                    {
                        // if ( !Abc_ObjIsTerm(pFanin) )
                        if ( pFanin->rawPos == 2 )
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->xPosTmp);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPosTmp);
                            minYPosFanin = fminf(minYPosFanin, pFanin->yPosTmp);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPosTmp);
                        }
                        else
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->xPos);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPos);
                            minYPosFanin = fminf(minYPosFanin, pFanin->yPos);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPos);
                        }
                    }
                }
                Abc_ObjForEachFanout( pFanin, pFaninOut, k )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFaninOut) )
                        continue;
                    if (pFaninOut->Id == 0)
                        continue;
                    if ( pFaninOut->rawPos == 1 || Abc_NodeIsTravIdCurrent(pFaninOut) )
                        continue;
                    // assert( Abc_ObjIsTerm(pFaninOut) || pFaninOut->rawPos == 2 );
                    // if ( !Abc_ObjIsTerm(pFaninOut) )
                    if ( pFaninOut->rawPos == 2 )
                    {
                        minXPosFanin = fminf(minXPosFanin, pFaninOut->xPosTmp);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPosTmp);
                        minYPosFanin = fminf(minYPosFanin, pFaninOut->yPosTmp);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPosTmp);
                    }
                    else
                    {
                        minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
                        minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
                    }
                }

                if (minXPosFanin < 900000)
                {
                    assert( nEndPoints + 1 < nEndPointCap );
                    for ( k = 0; k < nEndPoints; k++ )
                        if ( xPosSet[k] >= minXPosFanin )
                            break;
                    for ( l = nEndPoints; l > k; l-- )
                        xPosSet[l] = xPosSet[l - 1];
                    xPosSet[k] = minXPosFanin;

                    for ( ++k; k <= nEndPoints; k++ )
                        if ( xPosSet[k] >= maxXPosFanin )
                            break;
                    for ( l = nEndPoints + 1; l > k; l-- )
                        xPosSet[l] = xPosSet[l - 1];
                    xPosSet[k] = maxXPosFanin;

                    for ( k = 0; k < nEndPoints; k++ )
                        if ( yPosSet[k] >= minYPosFanin )
                            break;
                    for ( l = nEndPoints; l > k; l-- )
                        yPosSet[l] = yPosSet[l - 1];
                    yPosSet[k] = minYPosFanin;

                    for ( ++k; k <= nEndPoints; k++ )
                        if ( yPosSet[k] >= maxYPosFanin )
                            break;
                    for ( l = nEndPoints + 1; l > k; l-- )
                        yPosSet[l] = yPosSet[l - 1];
                    yPosSet[k] = maxYPosFanin;
                    nEndPoints += 2;
                }
            }
            Abc_ObjForEachFanout( pNodeAig, pFanout, j )
            {   
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                if ( pFanout->rawPos == 1 || Abc_NodeIsTravIdCurrent(pFanout) )
                    continue;
                // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
                // if ( !Abc_ObjIsTerm(pFanout) )
                if ( pFanout->rawPos == 2 )
                {
                    minXPosFanout = fminf(minXPosFanout, pFanout->xPosTmp);
                    maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPosTmp);
                    minYPosFanout = fminf(minYPosFanout, pFanout->yPosTmp);
                    maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPosTmp);
                }
                else
                {
                    minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
                    maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
                    minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
                    maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
                }
            }
            // fanouts of pRoot will be added to pRootNew
            if ( i == nSizeCore - 1 )
            {
                assert( pNodeAig == Abc_ObjRegular(pRootNew) );
                Abc_ObjForEachFanout( pRoot, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
                    // if ( !Abc_ObjIsTerm(pFanout) )
                    if ( pFanout->rawPos == 2 )
                    {
                        minXPosFanout = fminf(minXPosFanout, pFanout->xPosTmp);
                        maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPosTmp);
                        minYPosFanout = fminf(minYPosFanout, pFanout->yPosTmp);
                        maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPosTmp);
                    }
                    else
                    {
                        minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
                        maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
                        minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
                        maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
                    }
                }
            }
            if (minXPosFanout < 900000)
            {
                assert( nEndPoints + 1 < nEndPointCap );
                for ( k = 0; k < nEndPoints; k++ )
                    if ( xPosSet[k] >= minXPosFanout )
                        break;
                for ( l = nEndPoints; l > k; l-- )
                    xPosSet[l] = xPosSet[l - 1];
                xPosSet[k] = minXPosFanout;

                for ( ++k; k <= nEndPoints; k++ )
                    if ( xPosSet[k] >= maxXPosFanout )
                        break;
                for ( l = nEndPoints + 1; l > k; l-- )
                    xPosSet[l] = xPosSet[l - 1];
                xPosSet[k] = maxXPosFanout;

                for ( k = 0; k < nEndPoints; k++ )
                    if ( yPosSet[k] >= minYPosFanout )
                        break;
                for ( l = nEndPoints; l > k; l-- )
                    yPosSet[l] = yPosSet[l - 1];
                yPosSet[k] = minYPosFanout;

                for ( ++k; k <= nEndPoints; k++ )
                    if ( yPosSet[k] >= maxYPosFanout )
                        break;
                for ( l = nEndPoints + 1; l > k; l-- )
                    yPosSet[l] = yPosSet[l - 1];
                yPosSet[k] = maxYPosFanout;
                nEndPoints += 2;
            }
            if (nEndPoints >= 2)
            {
                pNodeAig->rectLx = xPosSet[nEndPoints / 2 - 1];
                pNodeAig->rectRx = xPosSet[nEndPoints / 2];
                pNodeAig->rectLy = yPosSet[nEndPoints / 2 - 1];
                pNodeAig->rectRy = yPosSet[nEndPoints / 2];
            }
            else
            {
                // mark floating nodes
                pNodeAig->rectLx = pNodeAig->rectRx = pNodeAig->rectLy = pNodeAig->rectRy = -10000.56;
            }
        }
        for ( int i = nSizeCore - 1; i >= 0; --i )
        {   
            pNodeAig = rawNodesCore[i];
            Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                if ( pFanout->rawPos == 1 )
                    break;
            if ( j == Abc_ObjFanoutNum(pNodeAig) )
            {
                // posXNew = (pNodeAig->rectLx + pNodeAig->rectRx) / 2;
                // posYNew = (pNodeAig->rectLy + pNodeAig->rectRy) / 2;

                // skip floating nodes
                if ( pNodeAig->rectLx == -10000.56 )
                {
                    printf("Idle subgraph!!!!\n");
                    assert ( pFanin->rectRx == -10000.56 );
                    assert ( pFanin->rectLy == -10000.56 );
                    assert ( pFanin->rectRy == -10000.56 );
                    pNodeAig->xPosTmp = 0;
                    pNodeAig->yPosTmp = 0;
                    continue;
                }
                else
                {
                    // find the best position
                    // fine-grained
                    if (pNodeAig->rectLx < pNtk->binOriX + pNodeAig->half_den_sizeX)
                        pNodeAig->rectLx = pNtk->binOriX + pNodeAig->half_den_sizeX;
                    if (pNodeAig->rectRx < pNtk->binOriX + pNodeAig->half_den_sizeX)
                        pNodeAig->rectRx = pNtk->binOriX + pNodeAig->half_den_sizeX;
                    if (pNodeAig->rectLy < pNtk->binOriY + pNodeAig->half_den_sizeY)
                        pNodeAig->rectLy = pNtk->binOriY + pNodeAig->half_den_sizeY;
                    if (pNodeAig->rectRy < pNtk->binOriY + pNodeAig->half_den_sizeY)
                        pNodeAig->rectRy = pNtk->binOriY + pNodeAig->half_den_sizeY;
                    if (pNodeAig->rectLx > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
                        pNodeAig->rectLx = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
                    if (pNodeAig->rectRx > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
                        pNodeAig->rectRx = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
                    if (pNodeAig->rectLy > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
                        pNodeAig->rectLy = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
                    if (pNodeAig->rectRy > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
                        pNodeAig->rectRy = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
                    xStep = (pNodeAig->rectRx - pNodeAig->rectLx) / 5;
                    yStep = (pNodeAig->rectRy - pNodeAig->rectLy) / 5;
                    if ( xStep < 0.05 )
                    {
                        xStep = 0.05;
                        xCnt = (int)((pNodeAig->rectRx - pNodeAig->rectLx) / 0.05);
                    }
                    else
                        xCnt = 5;
                    if ( yStep < 0.05 )
                    {
                        yStep = 0.05;
                        yCnt = (int)((pNodeAig->rectRy - pNodeAig->rectLy) / 0.05);
                    }
                    else
                        yCnt = 5;
                    xBest = 0;
                    yBest = 0;
                    minDensity = FLT_MAX;
                    minOverflow = FLT_MAX;
                    for (int ii = 0; ii <= xCnt; ++ii)
                        for (int jj = 0; jj <= yCnt; ++jj)
                        {
                            curDensity = 0;
                            curOverflow = 0;
                            xMin = pNodeAig->rectLx + ii * xStep - pNodeAig->half_den_sizeX;
                            yMin = pNodeAig->rectLy + jj * yStep - pNodeAig->half_den_sizeY;
                            xMax = pNodeAig->rectLx + ii * xStep + pNodeAig->half_den_sizeX;
                            yMax = pNodeAig->rectLy + jj * yStep + pNodeAig->half_den_sizeY;
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
                                    curDensity += pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] * xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNodeAig->den_scal;
                                    curOverflow += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] + xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal)
                                                    - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                                }
                            }

                            if ( curOverflow < minOverflow )
                            {
                                xBest = ii;
                                yBest = jj;
                                minOverflow = curOverflow;
                                minDensity = curDensity;
                            }
                            else if ( curOverflow == minOverflow && curDensity < minDensity )
                            {
                                xBest = ii;
                                yBest = jj;
                                minDensity = curDensity;
                            }
                            else if ( curOverflow == minOverflow && curDensity == minDensity 
                                && (fabsf(ii - xCnt / 2) * fabsf(ii - xCnt / 2) + fabsf(jj - yCnt / 2) * fabsf(jj - yCnt / 2) < fabsf(xBest - xCnt / 2) * fabsf(xBest - xCnt / 2) + fabsf(yBest - yCnt / 2) * fabsf(yBest - yCnt / 2)) )
                            {
                                xBest = ii;
                                yBest = jj;
                            }
                        }
                    // density change
                    assert( nNew < 3001 );
                    idxNewTmp[nNew++] = pNodeAig->Id; 
                    posXNew = pNodeAig->rectLx + xBest * xStep;
                    posYNew = pNodeAig->rectLy + yBest * yStep;
                    
                    xMin = posXNew - pNodeAig->half_den_sizeX;
                    yMin = posYNew - pNodeAig->half_den_sizeY;
                    xMax = posXNew + pNodeAig->half_den_sizeX;
                    yMax = posYNew + pNodeAig->half_den_sizeY;
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

                        xShare[xIdx - xb0] = x1 - x0;
                    }

                    for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
                        y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

                        y0 = fmaxf(y0, yMin);

                        y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

                        y1 = fminf(y1, yMax);

                        yShare[yIdx - yb0] = y1 - y0;
                    }

                    for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
                        for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                            overflow += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] + xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal)
                                    - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                            pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
                        }
                    }
                    
                    posDiffCore += fabsf(posXNew - pNodeAig->xPosTmp) + fabsf(posYNew - pNodeAig->yPosTmp);
                    pNodeAig->xPosTmp = posXNew;
                    pNodeAig->yPosTmp = posYNew;
                    continue;
                }
            }
            minXPosFanout = 1000000;
            maxXPosFanout = -1000000;
            minYPosFanout = 1000000;
            maxYPosFanout = -1000000;
            nEndPoints = 0;
            Abc_ObjForEachFanin( pNodeAig, pFanin, j )
            {
                // IO floating mode: skip the terminals ****************************
                minXPosFanin = 1000000;
                maxXPosFanin = -1000000;
                minYPosFanin = 1000000;
                maxYPosFanin = -1000000;
                if ( !Abc_ObjIsTerm(pFanin) && pFanin->Id != 0 )
                {
                    if ( pFanin->rawPos == 1 )
                    {
                        // skip floating nodes
                        if ( pFanin->rectLx == -10000.56 )
                        {
                            assert ( pFanin->rectRx == -10000.56 );
                            assert ( pFanin->rectLy == -10000.56 );
                            assert ( pFanin->rectRy == -10000.56 );
                        }
                        else
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->rectLx);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->rectLx);
                            minXPosFanin = fminf(minXPosFanin, pFanin->rectRx);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->rectRx);
                            minYPosFanin = fminf(minYPosFanin, pFanin->rectLy);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->rectLy);
                            minYPosFanin = fminf(minYPosFanin, pFanin->rectRy);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->rectRy);
                        }
                    }
                    else
                    {
                        // if ( !Abc_ObjIsTerm(pFanin) )
                        if ( pFanin->rawPos == 2 )
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->xPosTmp);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPosTmp);
                            minYPosFanin = fminf(minYPosFanin, pFanin->yPosTmp);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPosTmp);
                        }
                        else
                        {
                            minXPosFanin = fminf(minXPosFanin, pFanin->xPos);
                            maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPos);
                            minYPosFanin = fminf(minYPosFanin, pFanin->yPos);
                            maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPos);
                        }
                    }
                }

                Abc_ObjForEachFanout( pFanin, pFaninOut, k )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFaninOut) )
                        continue;
                    if (pFaninOut->Id == 0)
                        continue;
                    if ( pFaninOut->rawPos == 1 || Abc_NodeIsTravIdCurrent(pFaninOut) )
                        continue;
                    // assert( Abc_ObjIsTerm(pFaninOut) || pFaninOut->rawPos == 2 );
                    // if ( !Abc_ObjIsTerm(pFaninOut) )
                    if ( pFaninOut->rawPos == 2 )
                    {
                        minXPosFanin = fminf(minXPosFanin, pFaninOut->xPosTmp);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPosTmp);
                        minYPosFanin = fminf(minYPosFanin, pFaninOut->yPosTmp);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPosTmp);
                    }
                    else
                    {
                        minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
                        minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
                    }
                }

                if (minXPosFanin < 900000)
                {
                    assert( nEndPoints + 1 < nEndPointCap );
                    for ( k = 0; k < nEndPoints; k++ )
                        if ( xPosSet[k] >= minXPosFanin )
                            break;
                    for ( l = nEndPoints; l > k; l-- )
                        xPosSet[l] = xPosSet[l - 1];
                    xPosSet[k] = minXPosFanin;

                    for ( ++k; k <= nEndPoints; k++ )
                        if ( xPosSet[k] >= maxXPosFanin )
                            break;
                    for ( l = nEndPoints + 1; l > k; l-- )
                        xPosSet[l] = xPosSet[l - 1];
                    xPosSet[k] = maxXPosFanin;

                    for ( k = 0; k < nEndPoints; k++ )
                        if ( yPosSet[k] >= minYPosFanin )
                            break;
                    for ( l = nEndPoints; l > k; l-- )
                        yPosSet[l] = yPosSet[l - 1];
                    yPosSet[k] = minYPosFanin;

                    for ( ++k; k <= nEndPoints; k++ )
                        if ( yPosSet[k] >= maxYPosFanin )
                            break;
                    for ( l = nEndPoints + 1; l > k; l-- )
                        yPosSet[l] = yPosSet[l - 1];
                    yPosSet[k] = maxYPosFanin;
                    nEndPoints += 2;
                }
            }
            Abc_ObjForEachFanout( pNodeAig, pFanout, j )
            {   
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                    continue;
                // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos );
                // if ( !Abc_ObjIsTerm(pFanout) )
                if ( pFanout->rawPos ) // core node or peripheral node
                {
                    minXPosFanout = fminf(minXPosFanout, pFanout->xPosTmp);
                    maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPosTmp);
                    minYPosFanout = fminf(minYPosFanout, pFanout->yPosTmp);
                    maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPosTmp);
                }
                else
                {
                    minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
                    maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
                    minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
                    maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
                }
            }

            if (minXPosFanout < 900000)
            {
                assert( nEndPoints + 1 < nEndPointCap );
                for ( k = 0; k < nEndPoints; k++ )
                    if ( xPosSet[k] >= minXPosFanout )
                        break;
                for ( l = nEndPoints; l > k; l-- )
                    xPosSet[l] = xPosSet[l - 1];
                xPosSet[k] = minXPosFanout;

                for ( ++k; k <= nEndPoints; k++ )
                    if ( xPosSet[k] >= maxXPosFanout )
                        break;
                for ( l = nEndPoints + 1; l > k; l-- )
                    xPosSet[l] = xPosSet[l - 1];
                xPosSet[k] = maxXPosFanout;

                for ( k = 0; k < nEndPoints; k++ )
                    if ( yPosSet[k] >= minYPosFanout )
                        break;
                for ( l = nEndPoints; l > k; l-- )
                    yPosSet[l] = yPosSet[l - 1];
                yPosSet[k] = minYPosFanout;

                for ( ++k; k <= nEndPoints; k++ )
                    if ( yPosSet[k] >= maxYPosFanout )
                        break;
                for ( l = nEndPoints + 1; l > k; l-- )
                    yPosSet[l] = yPosSet[l - 1];
                yPosSet[k] = maxYPosFanout;
                nEndPoints += 2;
            }

            // posXNew = (xPosSet[nEndPoints / 2 - 1] + xPosSet[nEndPoints / 2]) / 2;
            // posYNew = (yPosSet[nEndPoints / 2 - 1] + yPosSet[nEndPoints / 2]) / 2;

            // find the best position
            // fine-grained
            if (xPosSet[nEndPoints / 2 - 1] < pNtk->binOriX + pNodeAig->half_den_sizeX)
                xPosSet[nEndPoints / 2 - 1] = pNtk->binOriX + pNodeAig->half_den_sizeX;
            if (xPosSet[nEndPoints / 2] < pNtk->binOriX + pNodeAig->half_den_sizeX)
                xPosSet[nEndPoints / 2] = pNtk->binOriX + pNodeAig->half_den_sizeX;
            if (yPosSet[nEndPoints / 2 - 1] < pNtk->binOriY + pNodeAig->half_den_sizeY)
                yPosSet[nEndPoints / 2 - 1] = pNtk->binOriY + pNodeAig->half_den_sizeY;
            if (yPosSet[nEndPoints / 2] < pNtk->binOriY + pNodeAig->half_den_sizeY)
                yPosSet[nEndPoints / 2] = pNtk->binOriY + pNodeAig->half_den_sizeY;
            if (xPosSet[nEndPoints / 2 - 1] > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
                xPosSet[nEndPoints / 2 - 1] = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
            if (xPosSet[nEndPoints / 2] > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
                xPosSet[nEndPoints / 2] = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
            if (yPosSet[nEndPoints / 2 - 1] > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
                yPosSet[nEndPoints / 2 - 1] = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
            if (yPosSet[nEndPoints / 2] > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
                yPosSet[nEndPoints / 2] = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
            xStep = (xPosSet[nEndPoints / 2] - xPosSet[nEndPoints / 2 - 1]) / 5;
            yStep = (yPosSet[nEndPoints / 2] - yPosSet[nEndPoints / 2 - 1]) / 5;
            if ( xStep < 0.05 )
            {
                xStep = 0.05;
                xCnt = (int)((xPosSet[nEndPoints / 2] - xPosSet[nEndPoints / 2 - 1]) / 0.05);
            }
            else
                xCnt = 5;
            if ( yStep < 0.05 )
            {
                yStep = 0.05;
                yCnt = (int)((yPosSet[nEndPoints / 2] - yPosSet[nEndPoints / 2 - 1]) / 0.05);
            }
            else
                yCnt = 5;
            xBest = 0;
            yBest = 0;
            minDensity = FLT_MAX;
            minOverflow = FLT_MAX;
            for (int ii = 0; ii <= xCnt; ++ii)
                for (int jj = 0; jj <= yCnt; ++jj)
                {
                    curDensity = 0;
                    curOverflow = 0;
                    xMin = xPosSet[nEndPoints / 2 - 1] + ii * xStep - pNodeAig->half_den_sizeX;
                    yMin = yPosSet[nEndPoints / 2 - 1] + jj * yStep - pNodeAig->half_den_sizeY;
                    xMax = xPosSet[nEndPoints / 2 - 1] + ii * xStep + pNodeAig->half_den_sizeX;
                    yMax = yPosSet[nEndPoints / 2 - 1] + jj * yStep + pNodeAig->half_den_sizeY;
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

                        xShare[xIdx - xb0] = x1 - x0;
                    }

                    for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
                        y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

                        y0 = fmaxf(y0, yMin);

                        y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

                        y1 = fminf(y1, yMax);

                        yShare[yIdx - yb0] = y1 - y0;
                    }

                    for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
                        for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                            curDensity += pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] * xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNodeAig->den_scal;
                            curOverflow += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] + xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal)
                                            - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                        }
                    }

                    if ( curOverflow < minOverflow )
                    {
                        xBest = ii;
                        yBest = jj;
                        minOverflow = curOverflow;
                        minDensity = curDensity;
                    }
                    else if ( curOverflow == minOverflow && curDensity < minDensity )
                    {
                        xBest = ii;
                        yBest = jj;
                        minDensity = curDensity;
                    }
                    else if ( curOverflow == minOverflow && curDensity == minDensity 
                        && (fabsf(ii - xCnt / 2) * fabsf(ii - xCnt / 2) + fabsf(jj - yCnt / 2) * fabsf(jj - yCnt / 2) < fabsf(xBest - xCnt / 2) * fabsf(xBest - xCnt / 2) + fabsf(yBest - yCnt / 2) * fabsf(yBest - yCnt / 2)) )
                    {
                        xBest = ii;
                        yBest = jj;
                    }
                }

            // density change
            assert( nNew < 3001 );
            idxNewTmp[nNew++] = pNodeAig->Id; 
            posXNew = xPosSet[nEndPoints / 2 - 1] + xBest * xStep;
            posYNew = yPosSet[nEndPoints / 2 - 1] + yBest * yStep;
            
            xMin = posXNew - pNodeAig->half_den_sizeX;
            yMin = posYNew - pNodeAig->half_den_sizeY;
            xMax = posXNew + pNodeAig->half_den_sizeX;
            yMax = posYNew + pNodeAig->half_den_sizeY;
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

                xShare[xIdx - xb0] = x1 - x0;
            }

            for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
                y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

                y0 = fmaxf(y0, yMin);

                y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

                y1 = fminf(y1, yMax);

                yShare[yIdx - yb0] = y1 - y0;
            }

            for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
                for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                    overflow += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] + xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal)
                                - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                    pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
                }
            }

            posDiffCore += fabsf(posXNew - pNodeAig->xPosTmp) + fabsf(posYNew - pNodeAig->yPosTmp);
            pNodeAig->xPosTmp = posXNew;
            pNodeAig->yPosTmp = posYNew;
        }
        
        // for ( int i = 0; i < nSizeCore; i++ )
        // {
        //     pNodeAig = rawNodesCore[i];
        //     // update the bounding box
        //     xGlobalMin = fminf(xGlobalMin, pNodeAig->xPosTmp);
        //     xGlobalMax = fmaxf(xGlobalMax, pNodeAig->xPosTmp);
        //     yGlobalMin = fminf(yGlobalMin, pNodeAig->yPosTmp);
        //     yGlobalMax = fmaxf(yGlobalMax, pNodeAig->yPosTmp);
        // }
            
        // early return, skip incremental placement ##############################
        if ( fSimple )
        {
            // node clustering: each node is assigned to its closet cluster F ~ m / dist^2
            if (fClustering)
                for ( i = 0; i < nSizeCore; ++i )
                {   
                    float maxForce = -1000, tmpForce;
                    int clusterId = -1;
                    pNodeAig = rawNodesCore[i];
                    for (j = 0; j < pNtk->nCellsPerCut; ++j)
                    {
                        tmpForce = Vec_IntEntry(&pNtk->vNodesPerCell, j) /
                            ((pNodeAig->xPosTmp - Vec_FltEntry(&pNtk->vXPosCell, j)) * (pNodeAig->xPosTmp - Vec_FltEntry(&pNtk->vXPosCell, j)) +
                             (pNodeAig->yPosTmp - Vec_FltEntry(&pNtk->vYPosCell, j)) * (pNodeAig->yPosTmp - Vec_FltEntry(&pNtk->vYPosCell, j)) + 0.001);
                        // tmpForce = pNtk->nNodesPerCell[j] * pNtk->nNodesPerCell[j] / ((pNodeAig->xPosTmp - pNtk->xPosCell[j])*(pNodeAig->xPosTmp - pNtk->xPosCell[j]) + (pNodeAig->yPosTmp - pNtk->yPosCell[j])*(pNodeAig->yPosTmp - pNtk->yPosCell[j]) + 0.001);
                        if (tmpForce > maxForce)
                        {
                            clusterId = j;
                            maxForce = tmpForce;
                        }
                    }
                    assert (clusterId != -1);
                    pNodeAig->xPosTmp = Vec_FltEntry(&pNtk->vXPosCell, clusterId);
                    pNodeAig->yPosTmp = Vec_FltEntry(&pNtk->vYPosCell, clusterId);
                }

            for ( i = 0; i < nSizeCore; ++i )
            {
                pNodeAig = rawNodesCore[i];
                Abc_ObjForEachFanin( pNodeAig, pFanin, j )
                    // if ( Abc_ObjIsTerm(pFanin) && !pFanin->rawPos )
                    if ( !pFanin->rawPos && !Abc_NodeIsTravIdCurrent(pFanin) )
                        {
                            if (pFanin->Id == 0)
                                continue;
                            assert( nSizeExtra < 3001 );
                            rawNodesExtra[nSizeExtra++] = pFanin;
                            pFanin->rawPos = 3;
                        }
            }
            // printf("nSizeExtra1 = %d\n", nSizeExtra);
            for ( i = 0; i < nSizePeri; ++i )
            {
                pNodeAig = rawNodesPeri[i];
                // Abc_ObjForEachFanin( pNodeAig, pFanin, j )
                //     printf("%d/%d/%d/%d & ", pNodeAig->Id, pFanin->Id, pFanin->rawPos, Abc_NodeIsTravIdCurrent(pFanin));
                Abc_ObjForEachFanin( pNodeAig, pFanin, j )
                    if ( !pFanin->rawPos && !Abc_NodeIsTravIdCurrent(pFanin) )
                        {
                            if (pFanin->Id == 0)
                                continue;
                            assert( nSizeExtra < 3001 );
                            rawNodesExtra[nSizeExtra++] = pFanin;
                            pFanin->rawPos = 3;
                            // printf("%d ** ", pFanin->Id);
                        }
            }
            // printf("nSizeExtra2 = %d\n", nSizeExtra);
            // printf("nSizePeri2 = %d\n", nSizePeri);
            for ( i = 0; i < nSizeExtra; ++i )
            {
                pNodeAig = rawNodesExtra[i];
                assert( pNodeAig->rawPos == 3 );
                pNodeAig->rawPos = 0;
            }
            // caculated local HPWL before and afer increamental placement
            for ( i = 0; i < nSizeCore; ++i )
            {
                pNodeAig = rawNodesCore[i];
                xMin = xMax = pNodeAig->xPos;
                yMin = yMax = pNodeAig->yPos;
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                    yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                    xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                    yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                }
                if ( i == nSizeCore - 1 )
                {
                    assert( pNodeAig == Abc_ObjRegular(pRootNew) );
                    Abc_ObjForEachFanout( pRoot, pFanout, j )
                    {
                        // IO floating mode: skip the terminals ****************************
                        if ( Abc_ObjIsTerm(pFanout) )
                            continue;
                        if (pFanout->Id == 0)
                            continue;
                        // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
                        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                    }
                }
                totalHPWL1 += xMax - xMin + yMax - yMin;

                xMin = xMax = pNodeAig->xPosTmp;
                yMin = yMax = pNodeAig->yPosTmp;
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
                    {
                        xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                        yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                        xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                        yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
                    }
                    else
                    {
                        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                    }
                }
                if ( i == nSizeCore - 1 )
                {
                    Abc_ObjForEachFanout( pRoot, pFanout, j )
                    {
                        // IO floating mode: skip the terminals ****************************
                        if ( Abc_ObjIsTerm(pFanout) )
                            continue;
                        if (pFanout->Id == 0)
                            continue;
                        if ( pFanout->rawPos == 2 )
                        {
                            xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                            yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                            xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                            yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
                        }
                        else
                        {
                            xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                            yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                            xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                            yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                        }
                    }
                }
                totalHPWL2 += xMax - xMin + yMax - yMin;
            }
            // printf("totalHPWL1 = %f\n", totalHPWL1);
            // printf("totalHPWL2 = %f\n", totalHPWL2);
            // printf("Diff = %f\n", totalHPWL1 - totalHPWL2);

            for ( i = 0; i < nSizePeri; ++i )
            {
                pNodeAig = rawNodesPeri[i];
                xMin = xMax = pNodeAig->xPos;
                yMin = yMax = pNodeAig->yPos;
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                    yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                    xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                    yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                }
                totalHPWL1 += xMax - xMin + yMax - yMin;

                xMin = xMax = pNodeAig->xPosTmp;
                yMin = yMax = pNodeAig->yPosTmp;
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
                    {
                        xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                        yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                        xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                        yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
                    }
                    else
                    {
                        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                    }
                }
                totalHPWL2 += xMax - xMin + yMax - yMin;
            }
            // printf("totalHPWL1 = %f\n", totalHPWL1);
            // printf("totalHPWL2 = %f\n", totalHPWL2);
            for ( i = 0; i < nSizeExtra; ++i )
            {
                pNodeAig = rawNodesExtra[i];
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pNodeAig) )
                {
                    xMin = 1000000;
                    yMin = 1000000;
                    xMax = -1000000;
                    yMax = -1000000;
                }
                else
                {
                    xMin = xMax = pNodeAig->xPos;
                    yMin = yMax = pNodeAig->yPos;
                }
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                    yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                    xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                    yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                }
                totalHPWL1 += xMax - xMin + yMax - yMin;

                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pNodeAig) )
                {
                    xMin = 1000000;
                    yMin = 1000000;
                    xMax = -1000000;
                    yMax = -1000000;
                }
                else
                {
                    xMin = xMax = pNodeAig->xPos;
                    yMin = yMax = pNodeAig->yPos;
                }
                Abc_ObjForEachFanout( pNodeAig, pFanout, j )
                {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFanout) )
                        continue;
                    if (pFanout->Id == 0)
                        continue;
                    if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                        continue;
                    if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
                    {
                        xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                        yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                        xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                        yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
                    }
                    else
                    {
                        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                    }
                }
                totalHPWL2 += xMax - xMin + yMax - yMin;
            }
            // printf("totalHPWL1 = %f\n", totalHPWL1);
            // printf("totalHPWL2 = %f\n", totalHPWL2);
            // printf("Diff = %f\n", totalHPWL1 - totalHPWL2);

            nTimingSaves = Dec_TimingUpdateVirtualCandidate( pGraph, pRoot, pRootNew, rawNodesCore, nSizeCore, TimingSaves,
                pRootArrivalT, pRootReqT, pRootArrivalG, pRootReqG );
            Dec_TimingRestore( TimingSaves, nTimingSaves );

            for ( i = 0; i < nSizeCore; ++i )
            {
                pNodeAig = rawNodesCore[i];
                pNodeAig->rawPos = 0;
            }
            for ( i = 0; i < nSizePeri; ++i )
            {
                pNodeAig = rawNodesPeri[i];
                pNodeAig->rawPos = 0;
            }
            
            if ( pRoot->Id == 68272 )
                printf("DDDDD");
            if ( added && Abc_ObjRegular(pRootNew)->Id != 0 )
                Abc_AigDeleteNode(pMan, Abc_ObjRegular(pRootNew));

            return increHPWL1 + increHPWL2 + totalHPWL2 - totalHPWL1;
        }


        // collect geometrically close nodes, which are in the bounding box
        int nSizePeriTmp = nSizePeri;
        int fflag;
        Abc_NtkForEachNode(pNtk, pNodeAig, i)
            if ( !Abc_ObjIsTerm(pNodeAig) && !pNodeAig->rawPos && !Abc_NodeIsTravIdCurrent(pNodeAig) )
                {
                    fflag = 0;
                    for ( int j = 0; j < nSizeCore; j++ )
                    {
                        Abc_Obj_t * pNodeTmp = rawNodesCore[j];
                        xGlobalMin = pNodeTmp->xPosTmp - 1 * pNtk->binStepX;
                        xGlobalMax = pNodeTmp->xPosTmp + 1 * pNtk->binStepX;
                        yGlobalMin = pNodeTmp->yPosTmp - 1 * pNtk->binStepY;
                        yGlobalMax = pNodeTmp->yPosTmp + 1 * pNtk->binStepY;
                        if (pNodeAig->xPos >= xGlobalMin && pNodeAig->xPos <= xGlobalMax && pNodeAig->yPos >= yGlobalMin && pNodeAig->yPos <= yGlobalMax)
                        {
                            assert( nSizePeri < nPeriCap );
                            rawNodesPeri[nSizePeri++] = pNodeAig;
                            pNodeAig->xPosTmp = pNodeAig->xPos;
                            pNodeAig->yPosTmp = pNodeAig->yPos;
                            pNodeAig->rawPos = 2;
                            fflag = 1;
                            break;
                        }
                    }
                    if ( !fflag )
                        for ( int j = 0; j < nSizePeriTmp; j++ )
                        {
                            Abc_Obj_t * pNodeTmp = rawNodesPeri[j];
                            xGlobalMin = pNodeTmp->xPosTmp - 0.5 * pNtk->binStepX;
                            xGlobalMax = pNodeTmp->xPosTmp + 0.5 * pNtk->binStepX;
                            yGlobalMin = pNodeTmp->yPosTmp - 0.5 * pNtk->binStepY;
                            yGlobalMax = pNodeTmp->yPosTmp + 0.5 * pNtk->binStepY;
                            if (pNodeAig->xPos >= xGlobalMin && pNodeAig->xPos <= xGlobalMax && pNodeAig->yPos >= yGlobalMin && pNodeAig->yPos <= yGlobalMax)
                            {
                                assert( nSizePeri < nPeriCap );
                                rawNodesPeri[nSizePeri++] = pNodeAig;
                                pNodeAig->xPosTmp = pNodeAig->xPos;
                                pNodeAig->yPosTmp = pNodeAig->yPos;
                                pNodeAig->rawPos = 2;
                                fflag = 1;
                                break;
                            }
                        }
                    if (nSizeCore + nSizePeri > 2000)
                        break;
                }
        // printf("nSizePeri2 = %d\n", nSizePeri);
        // printf("xGlobalMin = %f, xGlobalMax = %f, yGlobalMin = %f, yGlobalMax = %f\n", xGlobalMin, xGlobalMax, yGlobalMin, yGlobalMax);

        // preprocess for peripheral nodes
        for ( int i = 0; i < nSizePeri; i++ )
        {
            pNodeAig = rawNodesPeri[i];
            assert( nOld < 3001 );
            assert( nNew < 3001 );
            idxOldTmp[nOld++] = pNodeAig->Id;
            idxNewTmp[nNew++] = pNodeAig->Id;
        }
    //     // Peripheral nodes
    //     for ( int i = 0; i < nSizePeri; i++ )
    //     {
    //         minXPosFanout = 1000000;
    //         maxXPosFanout = -1000000;
    //         minYPosFanout = 1000000;
    //         maxYPosFanout = -1000000;
    //         nEndPoints = 0;
    //         pNodeAig = rawNodesPeri[i];

    //         // density change
    //         idxOldTmp[nOld++] = pNodeAig->Id; 
    //         xMin = pNodeAig->xPos - pNodeAig->half_den_sizeX;
    //         yMin = pNodeAig->yPos - pNodeAig->half_den_sizeY;
    //         xMax = pNodeAig->xPos + pNodeAig->half_den_sizeX;
    //         yMax = pNodeAig->yPos + pNodeAig->half_den_sizeY;
    //         xb0 = (int)((xMin - pNtk->binOriX) / pNtk->binStepX);
    //         yb0 = (int)((yMin - pNtk->binOriY) / pNtk->binStepY);
    //         xb1 = (int)((xMax - pNtk->binOriX) / pNtk->binStepX);
    //         yb1 = (int)((yMax - pNtk->binOriY) / pNtk->binStepY);
    //         if (xb0 < 0)
    //             xb0 = 0;
    //         if (yb0 < 0)
    //             yb0 = 0;
    //         if (xb1 >= pNtk->binDimX)
    //             xb1 = pNtk->binDimX - 1;
    //         if (yb1 >= pNtk->binDimY)
    //             yb1 = pNtk->binDimY - 1;
            
    //         for( int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //             x0 = xIdx * pNtk->binStepX + pNtk->binOriX;

    //             x0 = fmaxf(x0, xMin);

    //             x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;

    //             x1 = fminf(x1, xMax);

    //             xShare[xIdx - xb0] = x1 - x0;
    //         }

    //         for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //             y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

    //             y0 = fmaxf(y0, yMin);

    //             y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

    //             y1 = fminf(y1, yMax);

    //             yShare[yIdx - yb0] = y1 - y0;
    //         }

    //         for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //             for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //                 overflow += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] - xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal)
    //                             - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
    //                 pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] -= xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
    //             }
    //         }

    //         Abc_ObjForEachFanin( pNodeAig, pFanin, j )
    //         {   
    //             if (pFanin->Id == 0)
    //                 continue;
    //             // replace the old pRoot with pRootNew
    //             if ( pFanin == pRoot )
    //                 pFanin = Abc_ObjRegular(pRootNew);
    //             minXPosFanin = 1000000;
    //             maxXPosFanin = -1000000;
    //             minYPosFanin = 1000000;
    //             maxYPosFanin = -1000000;
    //             if ( pFanin->rawPos )
    //             {
    //                 minXPosFanin = fminf(minXPosFanin, pFanin->xPosTmp);
    //                 maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPosTmp);
    //                 minYPosFanin = fminf(minYPosFanin, pFanin->yPosTmp);
    //                 maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPosTmp);
    //             }
    //             else
    //             {
    //                 minXPosFanin = fminf(minXPosFanin, pFanin->xPos);
    //                 maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPos);
    //                 minYPosFanin = fminf(minYPosFanin, pFanin->yPos);
    //                 maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPos);
    //             }
    //             Abc_ObjForEachFanout( pFanin, pFaninOut, k )
    //             {
    //                 if (pFaninOut->Id == 0)
    //                     continue;
    //                 if ( pFaninOut == pNodeAig || (pFaninOut->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFaninOut)) )
    //                     continue;
    //                 if ( pFaninOut->rawPos )
    //                 {
    //                     minXPosFanin = fminf(minXPosFanin, pFaninOut->xPosTmp);
    //                     maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPosTmp);
    //                     minYPosFanin = fminf(minYPosFanin, pFaninOut->yPosTmp);
    //                     maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPosTmp);
    //                 }
    //                 else
    //                 {
    //                     minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
    //                     maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
    //                     minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
    //                     maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
    //                 }
    //             }
    //             if ( pFanin == Abc_ObjRegular(pRootNew) )
    //             {
    //                 // printf("\n%d, %d, %d\n", pNodeAig->Id, Abc_ObjFanoutNum(pFanin), Abc_ObjFanoutNum(pRoot));
    //                 Abc_ObjForEachFanout( pRoot, pFaninOut, k )
    //                 {
    //                     if (pFaninOut->Id == 0)
    //                         continue;
    //                     assert( Abc_ObjIsTerm(pFaninOut) || pFaninOut->rawPos == 2 );
    //                     if ( pFaninOut == pNodeAig )
    //                         continue;
    //                     if ( !Abc_ObjIsTerm(pFaninOut) )
    //                     {
    //                         minXPosFanin = fminf(minXPosFanin, pFaninOut->xPosTmp);
    //                         maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPosTmp);
    //                         minYPosFanin = fminf(minYPosFanin, pFaninOut->yPosTmp);
    //                         maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPosTmp);
    //                     }
    //                     else
    //                     {
    //                         minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
    //                         maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
    //                         minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
    //                         maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
    //                     }
    //                 }
    //             }

    //             assert(nEndPoints <= 100);
    //             for ( k = 0; k < nEndPoints; k++ )
    //                 if ( xPosSet[k] >= minXPosFanin )
    //                     break;
    //             for ( l = nEndPoints; l > k; l-- )
    //                 xPosSet[l] = xPosSet[l - 1];
    //             xPosSet[k] = minXPosFanin;

    //             for ( ++k; k <= nEndPoints; k++ )
    //                 if ( xPosSet[k] >= maxXPosFanin )
    //                     break;
    //             for ( l = nEndPoints + 1; l > k; l-- )
    //                 xPosSet[l] = xPosSet[l - 1];
    //             xPosSet[k] = maxXPosFanin;

    //             for ( k = 0; k < nEndPoints; k++ )
    //                 if ( yPosSet[k] >= minYPosFanin )
    //                     break;
    //             for ( l = nEndPoints; l > k; l-- )
    //                 yPosSet[l] = yPosSet[l - 1];
    //             yPosSet[k] = minYPosFanin;

    //             for ( ++k; k <= nEndPoints; k++ )
    //                 if ( yPosSet[k] >= maxYPosFanin )
    //                     break;
    //             for ( l = nEndPoints + 1; l > k; l-- )
    //                 yPosSet[l] = yPosSet[l - 1];
    //             yPosSet[k] = maxYPosFanin;
    //             nEndPoints += 2;
    //         }
    //         Abc_ObjForEachFanout( pNodeAig, pFanout, j )
    //         {
    //             if (pFanout->Id == 0)
    //                 continue;
    //             if ( (pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout)) )
    //                 continue;
    //             if ( pFanout->rawPos )
    //             {
    //                 minXPosFanout = fminf(minXPosFanout, pFanout->xPosTmp);
    //                 maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPosTmp);
    //                 minYPosFanout = fminf(minYPosFanout, pFanout->yPosTmp);
    //                 maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPosTmp);
    //             }
    //             else
    //             {
    //                 minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
    //                 maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
    //                 minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
    //                 maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
    //             }
    //         }
    //         if (minXPosFanout < 900000)
    //         {
    //             assert(nEndPoints <= 100);
    //             for ( k = 0; k < nEndPoints; k++ )
    //                 if ( xPosSet[k] >= minXPosFanout )
    //                     break;
    //             for ( l = nEndPoints; l > k; l-- )
    //                 xPosSet[l] = xPosSet[l - 1];
    //             xPosSet[k] = minXPosFanout;

    //             for ( ++k; k <= nEndPoints; k++ )
    //                 if ( xPosSet[k] >= maxXPosFanout )
    //                     break;
    //             for ( l = nEndPoints + 1; l > k; l-- )
    //                 xPosSet[l] = xPosSet[l - 1];
    //             xPosSet[k] = maxXPosFanout;

    //             for ( k = 0; k < nEndPoints; k++ )
    //                 if ( yPosSet[k] >= minYPosFanout )
    //                     break;
    //             for ( l = nEndPoints; l > k; l-- )
    //                 yPosSet[l] = yPosSet[l - 1];
    //             yPosSet[k] = minYPosFanout;

    //             for ( ++k; k <= nEndPoints; k++ )
    //                 if ( yPosSet[k] >= maxYPosFanout )
    //                     break;
    //             for ( l = nEndPoints + 1; l > k; l-- )
    //                 yPosSet[l] = yPosSet[l - 1];
    //             yPosSet[k] = maxYPosFanout;
    //             nEndPoints += 2;
    //         }
            
    //         // posXNew = (xPosSet[nEndPoints / 2 - 1] + xPosSet[nEndPoints / 2]) / 2;
    //         // posYNew = (yPosSet[nEndPoints / 2 - 1] + yPosSet[nEndPoints / 2]) / 2;

    //         // find the best position
    //         // fine-grained
    //         if (xPosSet[nEndPoints / 2 - 1] < pNtk->binOriX + pNodeAig->half_den_sizeX)
    //             xPosSet[nEndPoints / 2 - 1] = pNtk->binOriX + pNodeAig->half_den_sizeX;
    //         if (xPosSet[nEndPoints / 2] < pNtk->binOriX + pNodeAig->half_den_sizeX)
    //             xPosSet[nEndPoints / 2] = pNtk->binOriX + pNodeAig->half_den_sizeX;
    //         if (yPosSet[nEndPoints / 2 - 1] < pNtk->binOriY + pNodeAig->half_den_sizeY)
    //             yPosSet[nEndPoints / 2 - 1] = pNtk->binOriY + pNodeAig->half_den_sizeY;
    //         if (yPosSet[nEndPoints / 2] < pNtk->binOriY + pNodeAig->half_den_sizeY)
    //             yPosSet[nEndPoints / 2] = pNtk->binOriY + pNodeAig->half_den_sizeY;
    //         if (xPosSet[nEndPoints / 2 - 1] > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
    //             xPosSet[nEndPoints / 2 - 1] = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
    //         if (xPosSet[nEndPoints / 2] > pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX)
    //             xPosSet[nEndPoints / 2] = pNtk->binOriX + pNtk->binDimX * pNtk->binStepX - pNodeAig->half_den_sizeX;
    //         if (yPosSet[nEndPoints / 2 - 1] > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
    //             yPosSet[nEndPoints / 2 - 1] = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
    //         if (yPosSet[nEndPoints / 2] > pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY)
    //             yPosSet[nEndPoints / 2] = pNtk->binOriY + pNtk->binDimY * pNtk->binStepY - pNodeAig->half_den_sizeY;
    //         xStep = (xPosSet[nEndPoints / 2] - xPosSet[nEndPoints / 2 - 1]) / 5;
    //         yStep = (yPosSet[nEndPoints / 2] - yPosSet[nEndPoints / 2 - 1]) / 5;
    //         if ( xStep < 0.05 )
    //         {
    //             xStep = 0.05;
    //             xCnt = (int)((xPosSet[nEndPoints / 2] - xPosSet[nEndPoints / 2 - 1]) / 0.05);
    //         }
    //         else
    //             xCnt = 5;
    //         if ( yStep < 0.05 )
    //         {
    //             yStep = 0.05;
    //             yCnt = (int)((yPosSet[nEndPoints / 2] - yPosSet[nEndPoints / 2 - 1]) / 0.05);
    //         }
    //         else
    //             yCnt = 5;
    //         minDensity = 1000;
    //         minOverflow = 1000;
    //         for (int ii = 0; ii <= xCnt; ++ii)
    //             for (int jj = 0; jj <= yCnt; ++jj)
    //             {
    //                 curDensity = 0;
    //                 curOverflow = 0;
    //                 xMin = xPosSet[nEndPoints / 2 - 1] + ii * xStep - pNodeAig->half_den_sizeX;
    //                 yMin = yPosSet[nEndPoints / 2 - 1] + jj * yStep - pNodeAig->half_den_sizeY;
    //                 xMax = xPosSet[nEndPoints / 2 - 1] + ii * xStep + pNodeAig->half_den_sizeX;
    //                 yMax = yPosSet[nEndPoints / 2 - 1] + jj * yStep + pNodeAig->half_den_sizeY;
    //                 xb0 = (int)((xMin - pNtk->binOriX) / pNtk->binStepX);
    //                 yb0 = (int)((yMin - pNtk->binOriY) / pNtk->binStepY);
    //                 xb1 = (int)((xMax - pNtk->binOriX) / pNtk->binStepX);
    //                 yb1 = (int)((yMax - pNtk->binOriY) / pNtk->binStepY);
    //                 if (xb0 < 0)
    //                     xb0 = 0;
    //                 if (yb0 < 0)
    //                     yb0 = 0;
    //                 if (xb1 >= pNtk->binDimX)
    //                     xb1 = pNtk->binDimX - 1;
    //                 if (yb1 >= pNtk->binDimY)
    //                     yb1 = pNtk->binDimY - 1;
                    
    //                 for( int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //                     x0 = xIdx * pNtk->binStepX + pNtk->binOriX;

    //                     x0 = fmaxf(x0, xMin);

    //                     x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;

    //                     x1 = fminf(x1, xMax);

    //                     xShare[xIdx - xb0] = x1 - x0;
    //                 }

    //                 for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //                     y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

    //                     y0 = fmaxf(y0, yMin);

    //                     y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

    //                     y1 = fminf(y1, yMax);

    //                     yShare[yIdx - yb0] = y1 - y0;
    //                 }

    //                 for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //                     for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //                         curDensity += pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] * xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNodeAig->den_scal;
    //                         curOverflow += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] + xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal)
    //                                         - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
    //                     }
    //                 }

    //                 if ( curOverflow < minOverflow )
    //                 {
    //                     xBest = ii;
    //                     yBest = jj;
    //                     minOverflow = curOverflow;
    //                     minDensity = curDensity;
    //                 }
    //                 else if ( curOverflow == minOverflow && curDensity < minDensity )
    //                 {
    //                     xBest = ii;
    //                     yBest = jj;
    //                     minDensity = curDensity;
    //                 }
    //                 else if ( curOverflow == minOverflow && curDensity == minDensity 
    //                     && (fabsf(ii - xCnt / 2) * fabsf(ii - xCnt / 2) + fabsf(jj - yCnt / 2) * fabsf(jj - yCnt / 2) < fabsf(xBest - xCnt / 2) * fabsf(xBest - xCnt / 2) + fabsf(yBest - yCnt / 2) * fabsf(yBest - yCnt / 2)) )
    //                 {
    //                     xBest = ii;
    //                     yBest = jj;
    //                 }
    //             }

    //         // density change
    //         idxNewTmp[nNew++] = pNodeAig->Id; 
    //         posXNew = xPosSet[nEndPoints / 2 - 1] + xBest * xStep;
    //         posYNew = yPosSet[nEndPoints / 2 - 1] + yBest * yStep;
            
    //         xMin = posXNew - pNodeAig->half_den_sizeX;
    //         yMin = posYNew - pNodeAig->half_den_sizeY;
    //         xMax = posXNew + pNodeAig->half_den_sizeX;
    //         yMax = posYNew + pNodeAig->half_den_sizeY;
    //         xb0 = (int)((xMin - pNtk->binOriX) / pNtk->binStepX);
    //         yb0 = (int)((yMin - pNtk->binOriY) / pNtk->binStepY);
    //         xb1 = (int)((xMax - pNtk->binOriX) / pNtk->binStepX);
    //         yb1 = (int)((yMax - pNtk->binOriY) / pNtk->binStepY);
    //         if (xb0 < 0)
    //             xb0 = 0;
    //         if (yb0 < 0)
    //             yb0 = 0;
    //         if (xb1 >= pNtk->binDimX)
    //             xb1 = pNtk->binDimX - 1;
    //         if (yb1 >= pNtk->binDimY)
    //             yb1 = pNtk->binDimY - 1;
            
    //         for( int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //             x0 = xIdx * pNtk->binStepX + pNtk->binOriX;

    //             x0 = fmaxf(x0, xMin);

    //             x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;

    //             x1 = fminf(x1, xMax);

    //             xShare[xIdx - xb0] = x1 - x0;
    //         }

    //         for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //             y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

    //             y0 = fmaxf(y0, yMin);

    //             y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

    //             y1 = fminf(y1, yMax);

    //             yShare[yIdx - yb0] = y1 - y0;
    //         }

    //         for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
    //             for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
    //                 overflow += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] + xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal)
    //                             - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
    //                 pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
    //             }
    //         }

    //         posDiffPeri += fabsf(posXNew - pNodeAig->xPosTmp) + fabsf(posYNew - pNodeAig->yPosTmp);
    //         pNodeAig->xPosTmp = posXNew;
    //         pNodeAig->yPosTmp = posYNew;
    //         // printf("%d, %d & %.2f, %.2f & %.2f ** ", pNodeAig->Id, Abc_ObjFanoutNum(pNodeAig), pNodeAig->xPosTmp, pNodeAig->yPosTmp, posDiffPeri);
    //     }
    //     posDiffCore /= nSizeCore;
    //     posDiffPeri /= nSizePeri;
    //     // printf("Iter = %d\n", iter);
    //     // printf("posDiffCore = %f\n", posDiffCore);
    //     // printf("posDiffPeri = %f\n", posDiffPeri);
    }

    for ( i = 0; i < nSizeCore; ++i )
    {
        pNodeAig = rawNodesCore[i];
        Abc_ObjForEachFanin( pNodeAig, pFanin, j )
            if ( Abc_ObjIsTerm(pFanin) && !pFanin->rawPos )
                {
                    if (pFanin->Id == 0)
                        continue;
                    assert( nSizeExtra < 3001 );
                    rawNodesExtra[nSizeExtra++] = pFanin;
                    pFanin->rawPos = 3;
                }
    }
    // printf("nSizeExtra1 = %d\n", nSizeExtra);
    // printf("nSizePeri2 = %d\n", nSizePeri);
    for ( i = 0; i < nSizePeri; ++i )
    {
        pNodeAig = rawNodesPeri[i];
        // Abc_ObjForEachFanin( pNodeAig, pFanin, j )
        //     printf("%d/%d/%d/%d & ", pNodeAig->Id, pFanin->Id, pFanin->rawPos, Abc_NodeIsTravIdCurrent(pFanin));
        Abc_ObjForEachFanin( pNodeAig, pFanin, j )
            if ( !pFanin->rawPos && !Abc_NodeIsTravIdCurrent(pFanin) )
                {
                    if (pFanin->Id == 0)
                        continue;
                    assert( nSizeExtra < 3001 );
                    rawNodesExtra[nSizeExtra++] = pFanin;
                    pFanin->rawPos = 3;
                    // printf("%d ** ", pFanin->Id);
                }
    }
    // printf("nSizeExtra2 = %d\n", nSizeExtra);
    for ( i = 0; i < nSizeExtra; ++i )
    {
        pNodeAig = rawNodesExtra[i];
        assert( pNodeAig->rawPos == 3 );
        pNodeAig->rawPos = 0;
    }
    // printf("Iter = %d\n", iter);
    // printf("posDiffCore = %f\n", posDiffCore);
    // printf("posDiffPeri = %f\n", posDiffPeri);


    // incremental placement with simple placer ###################################
    float grad_x[3001], grad_y[3001];
    float grad_x1[3001], grad_y1[3001];
    float alpha = 10.0;
    float step_size = 0.3;
    float overflowTmp = 0;
    int nCoveredBins = 0;
    assert(nSizeCore + nSizePeri < 3001);

    for (int iter = 0; iter < 50; iter++)
    {
        overflowTmp = 0.0;
        nCoveredBins = 0;
        memset(grad_x, 0, (nSizeCore + nSizePeri) * sizeof(float));
        memset(grad_y, 0, (nSizeCore + nSizePeri) * sizeof(float));
        memset(grad_x1, 0, (nSizeCore + nSizePeri) * sizeof(float));
        memset(grad_y1, 0, (nSizeCore + nSizePeri) * sizeof(float));
        for ( int id = 0; id < nSizeCore + nSizePeri; ++id )
        {
            if (id < nSizeCore)
                pNodeAig = rawNodesCore[id];
            else 
                pNodeAig = rawNodesPeri[id - nSizeCore];

            // wireLength gradient
            // pNodeAig as a sink
            xMin = xMax = pNodeAig->xPosTmp;
            yMin = yMax = pNodeAig->yPosTmp;
            Abc_ObjForEachFanin( pNodeAig, pFanin, i) {
                // IO floating mode: skip the terminals ****************************
                if ( !Abc_ObjIsTerm(pFanin) && pFanin->Id != 0 )
                {
                    xMin = fminf(xMin, pFanin->xPosTmp);
                    xMax = fmaxf(xMax, pFanin->xPosTmp);
                    yMin = fminf(yMin, pFanin->yPosTmp);
                    yMax = fmaxf(yMax, pFanin->yPosTmp);
                }
                Abc_ObjForEachFanout(pFanin, pFaninOut, j) {
                    // IO floating mode: skip the terminals ****************************
                    if ( Abc_ObjIsTerm(pFaninOut) )
                        continue;
                    if (pFaninOut->Id == 0)
                        continue;
                    xMin = fminf(xMin, pFaninOut->xPosTmp);
                    xMax = fmaxf(xMax, pFaninOut->xPosTmp);
                    yMin = fminf(yMin, pFaninOut->yPosTmp);
                    yMax = fmaxf(yMax, pFaninOut->yPosTmp);
                }

                // X direction gradient
                if (fabsf(pNodeAig->xPosTmp - xMin) < 1e-9) {
                    // pNodeAig is at the left boundary (min_x)
                    grad_x[id] -= 1.0;  // Moving right decreases HPWL
                } else if (fabsf(pNodeAig->xPosTmp - xMax) < 1e-9) {
                    // pNodeAig is at the right boundary (max_x)
                    grad_x[id] += 1.0;  // Moving right increases HPWL
                }
                // If pNodeAig is in the middle, gradient is 0
                
                // Y direction gradient
                if (fabsf(pNodeAig->yPosTmp - yMin) < 1e-9) {
                    // pNodeAig is at the bottom boundary (min_y)
                    grad_y[id] -= 1.0;  // Moving up decreases HPWL
                } else if (fabsf(pNodeAig->yPosTmp - yMax) < 1e-9) {
                    // pNodeAig is at the top boundary (max_y)
                    grad_y[id] += 1.0;  // Moving up increases HPWL
                }
                // If pNodeAig is in the middle, gradient is 0
            }

            // pNodeAig as a driver
            xMin = xMax = pNodeAig->xPosTmp;
            yMin = yMax = pNodeAig->yPosTmp;
            Abc_ObjForEachFanout( pNodeAig, pFanout, i) {
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                xMin = fminf(xMin, pFanout->xPosTmp);
                xMax = fmaxf(xMax, pFanout->xPosTmp);
                yMin = fminf(yMin, pFanout->yPosTmp);
                yMax = fmaxf(yMax, pFanout->yPosTmp);

                // X direction gradient
                if (fabsf(pNodeAig->xPosTmp - xMin) < 1e-9) {
                    // pNodeAig is at the left boundary (min_x)
                    grad_x[id] -= 1.0;  // Moving right decreases HPWL
                } else if (fabsf(pNodeAig->xPosTmp - xMax) < 1e-9) {
                    // pNodeAig is at the right boundary (max_x)
                    grad_x[id] += 1.0;  // Moving right increases HPWL
                }
                // If pNodeAig is in the middle, gradient is 0
                
                // Y direction gradient
                if (fabsf(pNodeAig->yPosTmp - yMin) < 1e-9) {
                    // pNodeAig is at the bottom boundary (min_y)
                    grad_y[id] -= 1.0;  // Moving up decreases HPWL
                } else if (fabsf(pNodeAig->yPosTmp - yMax) < 1e-9) {
                    // pNodeAig is at the top boundary (max_y)
                    grad_y[id] += 1.0;  // Moving up increases HPWL
                }
                // If pNodeAig is in the middle, gradient is 0
            }

            // density gradient
            int bin_xMin = (int)((pNodeAig->xPosTmp - pNodeAig->half_den_sizeX - pNtk->binOriX) / pNtk->binStepX);
            int bin_yMin = (int)((pNodeAig->yPosTmp - pNodeAig->half_den_sizeY - pNtk->binOriY) / pNtk->binStepY);
            int bin_xMax = (int)((pNodeAig->xPosTmp + pNodeAig->half_den_sizeX - pNtk->binOriX) / pNtk->binStepX);
            int bin_yMax = (int)((pNodeAig->yPosTmp + pNodeAig->half_den_sizeY - pNtk->binOriY) / pNtk->binStepY);
            int bottom_idx, top_idx;
            int left_idx, right_idx;
            float overflow_bottom, overflow_top, overflow_left, overflow_right, overflow_avg = 0.0;

            assert (bin_xMax - bin_xMin >= 0 && bin_xMax - bin_xMin < 10);
            assert (bin_yMax - bin_yMin >= 0 && bin_yMax - bin_yMin < 10);
            if ( bin_xMax == pNtk->binDimX )
                bin_xMax--;
            if ( bin_yMax == pNtk->binDimY )
                bin_yMax--;
            
            // Calculate average overflow
            for( int xIdx = bin_xMin; xIdx <= bin_xMax; xIdx++) {
                x0 = xIdx * pNtk->binStepX + pNtk->binOriX;
                x0 = fmaxf(x0, pNodeAig->xPosTmp - pNodeAig->half_den_sizeX);
                x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;
                x1 = fminf(x1, pNodeAig->xPosTmp + pNodeAig->half_den_sizeX);
                xShare[xIdx - bin_xMin] = x1 - x0;
            }

            for( int yIdx = bin_yMin; yIdx <= bin_yMax; yIdx++) {
                y0 = yIdx * pNtk->binStepY + pNtk->binOriY;
                y0 = fmaxf(y0, pNodeAig->yPosTmp - pNodeAig->half_den_sizeY);
                y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;
                y1 = fminf(y1, pNodeAig->yPosTmp + pNodeAig->half_den_sizeY);
                yShare[yIdx - bin_yMin] = y1 - y0;
            }

            for(int xIdx = bin_xMin; xIdx <= bin_xMax; xIdx++) {
                for(int yIdx = bin_yMin; yIdx <= bin_yMax; yIdx++) {
                    overflow_avg += fmaxf(0.0, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] - 1.0) * xShare[xIdx - bin_xMin] * yShare[yIdx - bin_yMin];
                }
            }
            overflow_avg /= 4 * pNodeAig->half_den_sizeX * pNodeAig->half_den_sizeY;

            // Calculate gradient using central difference in neighboring bins
            for (int i = bin_yMin; i <= bin_yMax; ++i) {
                bottom_idx = bin_xMin * pNtk->binDimY + i;
                top_idx = bin_xMax * pNtk->binDimY + i;
                overflow_bottom = fmaxf(0.0, pNtk->binsDensity[bottom_idx] - 1.0);
                overflow_top = fmaxf(0.0, pNtk->binsDensity[top_idx] - 1.0);

                grad_x1[id] += (overflow_top - overflow_bottom + (pNtk->binsDensity[top_idx] - pNtk->binsDensity[bottom_idx]) * overflow_avg) * pNodeAig->den_scal;
            }

            for (int i = bin_xMin; i <= bin_xMax; ++i) {
                left_idx = i * pNtk->binDimY + bin_yMin;
                right_idx = i * pNtk->binDimY + bin_yMax;
                overflow_left = fmaxf(0.0, pNtk->binsDensity[left_idx] - 1.0);
                overflow_right = fmaxf(0.0, pNtk->binsDensity[right_idx] - 1.0);

                grad_y1[id] += (overflow_right - overflow_left + (pNtk->binsDensity[right_idx] - pNtk->binsDensity[left_idx]) * overflow_avg) * pNodeAig->den_scal;
            }

            grad_x[id] += alpha * grad_x1[id];
            grad_y[id] += alpha * grad_y1[id];
            float force_mag = sqrt(grad_x[id] * grad_x[id] + grad_y[id] * grad_y[id]);
            float max_force = fminf(pNtk->binStepX * pNtk->binDimX, pNtk->binStepY * pNtk->binDimY) / 100.0;
            if (force_mag > max_force) {
                grad_x[id] *= max_force / force_mag;
                grad_y[id] *= max_force / force_mag;
            }
        }
        for ( int id = 0; id < nSizeCore + nSizePeri; ++id )
        {
            if (id < nSizeCore)
                pNodeAig = rawNodesCore[id];
            else 
                pNodeAig = rawNodesPeri[id - nSizeCore];

            // density change after removing changed nodes
            xMin = pNodeAig->xPosTmp - pNodeAig->half_den_sizeX;
            yMin = pNodeAig->yPosTmp - pNodeAig->half_den_sizeY;
            xMax = pNodeAig->xPosTmp + pNodeAig->half_den_sizeX;
            yMax = pNodeAig->yPosTmp + pNodeAig->half_den_sizeY;
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

                xShare[xIdx - xb0] = x1 - x0;
            }

            for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
                y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

                y0 = fmaxf(y0, yMin);

                y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

                y1 = fminf(y1, yMax);

                yShare[yIdx - yb0] = y1 - y0;
            }

            for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
                for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                    pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] -= xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
                    // printf("3. %d, %f\n", xIdx * pNtk->binDimY + yIdx, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                }
            }
            // Update position
            pNodeAig->xPosTmp -= step_size * grad_x[id];
            pNodeAig->yPosTmp -= step_size * grad_y[id];
            // STRICT boundary enforcement
            float margin_x = pNodeAig->half_den_sizeX; // Extra margin
            float margin_y = pNodeAig->half_den_sizeY;
            pNodeAig->xPosTmp = fmaxf(pNtk->binOriX + margin_x, 
                            fminf(pNtk->binOriX + pNtk->binStepX * pNtk->binDimX - margin_x, pNodeAig->xPosTmp));
            pNodeAig->yPosTmp = fmaxf(pNtk->binOriY + margin_y, 
                            fminf(pNtk->binOriY + pNtk->binStepY * pNtk->binDimY - margin_y, pNodeAig->yPosTmp));
            // density change after placing changed nodes
            xMin = pNodeAig->xPosTmp - pNodeAig->half_den_sizeX;
            yMin = pNodeAig->yPosTmp - pNodeAig->half_den_sizeY;
            xMax = pNodeAig->xPosTmp + pNodeAig->half_den_sizeX;
            yMax = pNodeAig->yPosTmp + pNodeAig->half_den_sizeY;
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

                xShare[xIdx - xb0] = x1 - x0;
            }

            for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
                y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

                y0 = fmaxf(y0, yMin);

                y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

                y1 = fminf(y1, yMax);

                yShare[yIdx - yb0] = y1 - y0;
            }

            for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
                for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                    pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeAig->den_scal;
                    // printf("3. %d, %f\n", xIdx * pNtk->binDimY + yIdx, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                    overflowTmp += fmaxf(0.0, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] - 1.0);
                    nCoveredBins++;
                }
            }
        }
        // float overflow = 0.0;
        // for (int i = 0; i < pNtk->nBins; i++) {
        //     overflow += fmaxf(0.0, pNtk->binsDensity[i] - 1.0);
        // }
        overflowTmp *= pNtk->binModuleRatio * pNtk->nBins / (float)nCoveredBins;
        if (overflowTmp > 0.15) {
            alpha *= 1.5;  // More aggressive than before
            // std::cout << "  Density violation! Increased alpha to " << alpha << std::endl;
        } 
        else if (overflowTmp > 0.1) {
            alpha *= 1.2;
            step_size *= 0.9;
        }
        else {
            alpha *= 0.8; // Reduce penalty when density is good
            step_size *= 0.8;
        }
        if (step_size < 0.01)
                break;
        // if (iter == 0)
        //     printf("%d, %f, %f, %f\n", iter, overflowTmp, alpha, step_size);
    }
    // printf("%d, %f, %f, %f\n", iter, overflowTmp, alpha, step_size);
    // end incremental placement with simple placer ###################################



    // caculated local HPWL before and afer increamental placement
    for ( i = 0; i < nSizeCore; ++i )
    {
        pNodeAig = rawNodesCore[i];
        xMin = xMax = pNodeAig->xPos;
        yMin = yMax = pNodeAig->yPos;
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
        }
        if ( i == nSizeCore - 1 )
        {
            assert( pNodeAig == Abc_ObjRegular(pRootNew) );
            Abc_ObjForEachFanout( pRoot, pFanout, j )
            {
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                // assert( Abc_ObjIsTerm(pFanout) || pFanout->rawPos == 2 );
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
        }
        totalHPWL1 += xMax - xMin + yMax - yMin;

        xMin = xMax = pNodeAig->xPosTmp;
        yMin = yMax = pNodeAig->yPosTmp;
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
            {
                xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
            }
            else
            {
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
        }
        if ( i == nSizeCore - 1 )
        {
            Abc_ObjForEachFanout( pRoot, pFanout, j )
            {
                // IO floating mode: skip the terminals ****************************
                if ( Abc_ObjIsTerm(pFanout) )
                    continue;
                if (pFanout->Id == 0)
                    continue;
                if ( pFanout->rawPos == 2 )
                {
                    xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                    yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                    xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                    yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
                }
                else
                {
                    xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                    yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                    xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                    yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
                }
            }
        }
        totalHPWL2 += xMax - xMin + yMax - yMin;
    }
    // printf("totalHPWL1 = %f\n", totalHPWL1);
    // printf("totalHPWL2 = %f\n", totalHPWL2);
    for ( i = 0; i < nSizePeri; ++i )
    {
        pNodeAig = rawNodesPeri[i];
        xMin = xMax = pNodeAig->xPos;
        yMin = yMax = pNodeAig->yPos;
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
        }
        totalHPWL1 += xMax - xMin + yMax - yMin;

        xMin = xMax = pNodeAig->xPosTmp;
        yMin = yMax = pNodeAig->yPosTmp;
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
            {
                xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
            }
            else
            {
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
        }
        totalHPWL2 += xMax - xMin + yMax - yMin;
    }
    // printf("totalHPWL1 = %f\n", totalHPWL1);
    // printf("totalHPWL2 = %f\n", totalHPWL2);
    for ( i = 0; i < nSizeExtra; ++i )
    {
        pNodeAig = rawNodesExtra[i];
        // IO floating mode: skip the terminals ****************************
        if ( Abc_ObjIsTerm(pNodeAig) )
        {
            xMin = 1000000;
            yMin = 1000000;
            xMax = -1000000;
            yMax = -1000000;
        }
        else
        {
            xMin = xMax = pNodeAig->xPos;
            yMin = yMax = pNodeAig->yPos;
        }
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
        }
        totalHPWL1 += xMax - xMin + yMax - yMin;

        if ( Abc_ObjIsTerm(pNodeAig) )
        {
            xMin = 1000000;
            yMin = 1000000;
            xMax = -1000000;
            yMax = -1000000;
        }
        else
        {
            xMin = xMax = pNodeAig->xPos;
            yMin = yMax = pNodeAig->yPos;
        }
        Abc_ObjForEachFanout( pNodeAig, pFanout, j )
        {
            // IO floating mode: skip the terminals ****************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            if (pFanout->Id == 0)
                continue;
            if ( pFanout->rawPos == 0 && Abc_NodeIsTravIdCurrent(pFanout) )
                continue;
            if ( pFanout->rawPos == 1 || pFanout->rawPos == 2 )
            {
                xMin = ( pFanout->xPosTmp < xMin ) ? pFanout->xPosTmp : xMin;
                yMin = ( pFanout->yPosTmp < yMin ) ? pFanout->yPosTmp : yMin;
                xMax = ( pFanout->xPosTmp > xMax ) ? pFanout->xPosTmp : xMax;
                yMax = ( pFanout->yPosTmp > yMax ) ? pFanout->yPosTmp : yMax;
            }
            else
            {
                xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
                yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
                xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
                yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
            }
        }
        totalHPWL2 += xMax - xMin + yMax - yMin;
    }
    // printf("totalHPWL1 = %f\n", totalHPWL1);
    // printf("totalHPWL2 = %f\n", totalHPWL2);

    nTimingSaves = Dec_TimingUpdateVirtualCandidate( pGraph, pRoot, pRootNew, rawNodesCore, nSizeCore, TimingSaves,
        pRootArrivalT, pRootReqT, pRootArrivalG, pRootReqG );
    Dec_TimingRestore( TimingSaves, nTimingSaves );

    for ( i = 0; i < nSizeCore; ++i )
        rawNodesCore[i]->rawPos = 0;
    for ( i = 0; i < nSizePeri; ++i )
        rawNodesPeri[i]->rawPos = 0;

    // overflow calculation
    // float overflow2 = 0;
    // for (int i = 0; i < pNtk->nBins; ++i)
    // {
    //     // printf("%.2f && ", pNtk->binsDensity[i]);
    //     overflow2 += (pNtk->binsDensity[i] > 1) ? pNtk->binsDensity[i] - 1 : 0;
    // }
    // overflow2 *= pNtk->binModuleRatio;

    // density recovery
    // add old nodes
    assert (nOld < 3001);
    for (int i = 0; i < nOld; ++i)
    {
        // if (idxOldTmp[i] > 1000000)
        //     printf("nOld=%d, %d\n", nOld, i);
        pNodeR = Abc_NtkObj(pNtk, idxOldTmp[i]);
        xMin = pNodeR->xPos - pNodeR->half_den_sizeX;
        yMin = pNodeR->yPos - pNodeR->half_den_sizeY;
        xMax = pNodeR->xPos + pNodeR->half_den_sizeX;
        yMax = pNodeR->yPos + pNodeR->half_den_sizeY;
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

            xShare[xIdx - xb0] = x1 - x0;
        }

        for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
            y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

            y0 = fmaxf(y0, yMin);

            y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

            y1 = fminf(y1, yMax);

            yShare[yIdx - yb0] = y1 - y0;
        }

        for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
            for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeR->den_scal;
            }
        }
    }
    // remove new nodes
    for (int i = 0; i < nNew; ++i)
    {
        pNodeR = Abc_NtkObj(pNtk, idxNewTmp[i]);
        xMin = pNodeR->xPosTmp - pNodeR->half_den_sizeX;
        yMin = pNodeR->yPosTmp - pNodeR->half_den_sizeY;
        xMax = pNodeR->xPosTmp + pNodeR->half_den_sizeX;
        yMax = pNodeR->yPosTmp + pNodeR->half_den_sizeY;
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

            xShare[xIdx - xb0] = x1 - x0;
        }

        for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
            y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

            y0 = fmaxf(y0, yMin);

            y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

            y1 = fminf(y1, yMax);

            yShare[yIdx - yb0] = y1 - y0;
        }

        for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
            for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] -= xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNodeR->den_scal;
            }
        }
    }
    if ( added && Abc_ObjRegular(pRootNew)->Id != 0 )
        Abc_AigDeleteNode(pMan, Abc_ObjRegular(pRootNew));

    // printf("%f, %f, %f\n", overflow2, overflow, overflowSaved);
    // if ( overflow2 - overflow > overflowSaved + 0.0001 || overflow2 > 0.0999 )
    // printf("%f\n", overflowAfter + overflow * pNtk->binModuleRatio);
    // assert (overflowAfter + overflow * pNtk->binModuleRatio == overflow2 );
    
    // if ( overflow * pNtk->binModuleRatio > overflowBefore - overflowAfter + 0.01 || overflowAfter + overflow * pNtk->binModuleRatio > 0.0999 )
    //     return -1;
    
    // printf("HPWLAdded = %.2f\n", increHPWL1 + increHPWL2 + totalHPWL2 - totalHPWL1);
    HPWLResult = increHPWL1 + increHPWL2 + totalHPWL2 - totalHPWL1;
    return HPWLResult;// - (overflow2 - overflow - overflowSaved) * 100000;
}
/**Function*************************************************************

  Synopsis    [Replaces MFFC of the node by the new factored form.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Dec_GraphUpdateNetwork( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain )
{
    extern Abc_Obj_t *    Dec_GraphToNetwork( Abc_Ntk_t * pNtk, Dec_Graph_t * pGraph );
    Abc_Obj_t * pRootNew;
    Abc_Ntk_t * pNtk = pRoot->pNtk;
    int nNodesNew, nNodesOld, RetValue;
    nNodesOld = Abc_NtkNodeNum(pNtk);
    // create the new structure of nodes
    pRootNew = Dec_GraphToNetwork( pNtk, pGraph );
    // remove the old nodes
    RetValue = Abc_AigReplace( (Abc_Aig_t *)pNtk->pManFunc, pRoot, pRootNew, fUpdateLevel );
    // compare the gains
    nNodesNew = Abc_NtkNodeNum(pNtk);
    //assert( nGain <= nNodesOld - nNodesNew );
    return RetValue;
}

int Dec_GraphUpdateNetworkP( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain, int fSimple, int fClustering )
{
    extern Abc_Obj_t *    Dec_GraphToNetwork2( Abc_Ntk_t * pNtk, Dec_Graph_t * pGraph, Abc_Obj_t * pRoot, int fSimple, int fClustering );
    Abc_Obj_t * pRootNew;
    Abc_Ntk_t * pNtk = pRoot->pNtk;
    int nNodesNew, nNodesOld, RetValue;
    nNodesOld = Abc_NtkNodeNum(pNtk);
    // create the new structure of nodes
    pRootNew = Dec_GraphToNetwork2( pNtk, pGraph, pRoot, fSimple, fClustering );
    Dec_TimingUpdateActualCandidate( pGraph, pRoot, pRootNew );
    // remove the old nodes
    RetValue = Abc_AigReplace( (Abc_Aig_t *)pNtk->pManFunc, pRoot, pRootNew, fUpdateLevel );
    // compare the gains
    nNodesNew = Abc_NtkNodeNum(pNtk);
    //assert( nGain <= nNodesOld - nNodesNew );
    
    return RetValue;
}

/**Function*************************************************************

  Synopsis    [Transforms the decomposition graph into the AIG.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Hop_Obj_t * Dec_GraphToNetworkAig( Hop_Man_t * pMan, Dec_Graph_t * pGraph )
{
    Dec_Node_t * pNode = NULL; // Suppress "might be used uninitialized"
    Hop_Obj_t * pAnd0, * pAnd1;
    int i;
    // check for constant function
    if ( Dec_GraphIsConst(pGraph) )
        return Hop_NotCond( Hop_ManConst1(pMan), Dec_GraphIsComplement(pGraph) );
    // check for a literal
    if ( Dec_GraphIsVar(pGraph) )
        return Hop_NotCond( (Hop_Obj_t *)Dec_GraphVar(pGraph)->pFunc, Dec_GraphIsComplement(pGraph) );
    // build the AIG nodes corresponding to the AND gates of the graph
    Dec_GraphForEachNode( pGraph, pNode, i )
    {
        pAnd0 = Hop_NotCond( (Hop_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge0.Node)->pFunc, pNode->eEdge0.fCompl ); 
        pAnd1 = Hop_NotCond( (Hop_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge1.Node)->pFunc, pNode->eEdge1.fCompl ); 
        pNode->pFunc = Hop_And( pMan, pAnd0, pAnd1 );
    }
    // complement the result if necessary
    return Hop_NotCond( (Hop_Obj_t *)pNode->pFunc, Dec_GraphIsComplement(pGraph) );
}

/**Function*************************************************************

  Synopsis    [Strashes one logic node using its SOP.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Hop_Obj_t * Dec_GraphFactorSop( Hop_Man_t * pMan, char * pSop )
{
    Hop_Obj_t * pFunc;
    Dec_Graph_t * pFForm;
    Dec_Node_t * pNode;
    int i;
    // perform factoring
    pFForm = Dec_Factor( pSop );
    // collect the fanins
    Dec_GraphForEachLeaf( pFForm, pNode, i )
        pNode->pFunc = Hop_IthVar( pMan, i );
    // perform strashing
    pFunc = Dec_GraphToNetworkAig( pMan, pFForm );
    Dec_GraphFree( pFForm );
    return pFunc;
}

/**Function*************************************************************

  Synopsis    [Transforms the decomposition graph into the AIG.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Ivy_Obj_t * Dec_GraphToNetworkIvy( Ivy_Man_t * pMan, Dec_Graph_t * pGraph )
{
    Dec_Node_t * pNode = NULL; // Suppress "might be used uninitialized"
    Ivy_Obj_t * pAnd0, * pAnd1;
    int i;
    // check for constant function
    if ( Dec_GraphIsConst(pGraph) )
        return Ivy_NotCond( Ivy_ManConst1(pMan), Dec_GraphIsComplement(pGraph) );
    // check for a literal
    if ( Dec_GraphIsVar(pGraph) )
        return Ivy_NotCond( (Ivy_Obj_t *)Dec_GraphVar(pGraph)->pFunc, Dec_GraphIsComplement(pGraph) );
    // build the AIG nodes corresponding to the AND gates of the graph
    Dec_GraphForEachNode( pGraph, pNode, i )
    {
        pAnd0 = Ivy_NotCond( (Ivy_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge0.Node)->pFunc, pNode->eEdge0.fCompl ); 
        pAnd1 = Ivy_NotCond( (Ivy_Obj_t *)Dec_GraphNode(pGraph, pNode->eEdge1.Node)->pFunc, pNode->eEdge1.fCompl ); 
        pNode->pFunc = Ivy_And( pMan, pAnd0, pAnd1 );
    }
    // complement the result if necessary
    return Ivy_NotCond( (Ivy_Obj_t *)pNode->pFunc, Dec_GraphIsComplement(pGraph) );
}


////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END
