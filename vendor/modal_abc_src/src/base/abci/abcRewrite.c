/**CFile****************************************************************

  FileName    [abcRewrite.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Network and node package.]

  Synopsis    [Technology-independent resynthesis of the AIG based on DAG aware rewriting.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: abcRewrite.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "base/abc/abc.h"
#include "opt/rwr/rwr.h"
#include "bool/dec/dec.h"

#include "math.h"
ABC_NAMESPACE_IMPL_START


/*
    The ideas realized in this package are inspired by the paper:
    Per Bjesse, Arne Boralv, "DAG-aware circuit compression for 
    formal verification", Proc. ICCAD 2004, pp. 42-49.
*/

////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

static Cut_Man_t * Abc_NtkStartCutManForRewrite( Abc_Ntk_t * pNtk );
static void        Abc_NodePrintCuts( Abc_Obj_t * pNode );
static void        Abc_ManShowCutCone( Abc_Obj_t * pNode, Vec_Ptr_t * vLeaves );
static void        Abc_NtkRewritePrecomputeTiming( Abc_Ntk_t * pNtk );

extern void  Abc_PlaceBegin( Abc_Ntk_t * pNtk );
extern void  Abc_PlaceEnd( Abc_Ntk_t * pNtk );
extern void  Abc_PlaceUpdate( Vec_Ptr_t * vAddedCells, Vec_Ptr_t * vUpdatedNets );

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

static inline float Abc_ObjWireDelay( Abc_Obj_t * pFrom, Abc_Obj_t * pTo )
{
    if ( Abc_ObjIsTerm(pFrom) || Abc_ObjIsTerm(pTo) )
        return 0.0f;
    float dx = pFrom->xPos - pTo->xPos;
    float dy = pFrom->yPos - pTo->yPos;
    return sqrtf( dx * dx + dy * dy );
}

static void Abc_NtkRewritePrecomputeTiming( Abc_Ntk_t * pNtk )
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
            float CandidateG = pFanin->arrivalG + Abc_ObjWireDelay( pFanin, pNode );
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
                float CandidateG = pNode->reqG - Abc_ObjWireDelay( pFanin, pNode );
                if ( pFanin->reqG > CandidateG )
                    pFanin->reqG = CandidateG;
            }
        }
    }
}

/**Function*************************************************************

  Synopsis    [Performs incremental rewriting of the AIG.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int incrementalPlacement( Abc_Ntk_t * pNtk, int order, int nSizeMax, int fHPWL )
{
    Abc_Obj_t * rawNodes[100000];
    Abc_Obj_t * pNode;
    Abc_Obj_t * pFanin, * pFanout, * pFaninOut;
    int i, j, k, l, nFans, nSizeTmp, nSizeTmp2, nSize = 0;
    int isFirst = 1, iter = 0;
    float avgXPos, avgYPos, minXPosFanin, maxXPosFanin, minYPosFanin, maxYPosFanin, minXPosFanout, maxXPosFanout, minYPosFanout, maxYPosFanout;
    float posDiff = 100, posDiff2 = 100, posDiff3 = 100;
    float posXNew, posYNew;
    float xPosSet[10], yPosSet[10];
    float rectLx[100000], rectRx[100000], rectLy[100000], rectRy[100000];
    int nEndPoints;
    int nFanouts;
    int nNew = 10;

    // Abc_NtkForEachNodeReverse( pNtk, pNode, i )
    // while (nNew > 0)
    // {
    //     nNew = 0;
    //     Abc_NtkForEachNode( pNtk, pNode, i )
    //     {   
    //         // if ( nSize == nSizeMax )
    //         //     break;
    //         if ( pNode->rawPos )
    //         {   
    //             Abc_ObjForEachFanin( pNode, pFanin, j )
    //             {
    //                 if ( Abc_ObjIsTerm(pFanin) || pFanin->rawPos )
    //                     continue;
    //                 nFanouts = 0;
    //                 Abc_ObjForEachFanout( pFanin, pFaninOut, k )
    //                     if (pFaninOut->rawPos)
    //                         nFanouts++;
    //                 if ( nFanouts == 1 )
    //                 {
    //                     pFanin->rawPos = 1;
    //                     nNew++;
    //                 }
    //                 // rawNodes[nSize++] = pFanin;
    //             }
    //             // rawNodes[nSize++] = pNode;
    //             // printf("%d\n", pNode->Id);
    //         }
    //     }
    //     printf("%d\n", nNew);
    // }

    Abc_NtkForEachNode( pNtk, pNode, i )
    {   
        // if ( nSize == nSizeMax )
        //     break;
        if ( pNode->rawPos )
        {   
            rawNodes[nSize++] = pNode;
            // printf("%d\n", pNode->Id);
        }
    }
    nSizeMax = nSize + nSizeMax; // maximum number of total nodes to be replaced
    // for ( int i = 0; i < nSize; ++i )
    // {
    //     printf("%d, %d, %f, %f * ", rawNodes[i]->Id, rawNodes[i]->Type, rawNodes[i]->xPos, rawNodes[i]->yPos);
    // }
    // printf("\n");

    while ( posDiff > pow(10.0, -order) )
    {   
        nSize = 0;
        Abc_NtkForEachNode( pNtk, pNode, i )
        {   
            // if ( nSize == nSizeMax )
            //     break;
            if ( pNode->rawPos )
            {
                rawNodes[nSize++] = pNode;
                // printf("%d\n", pNode->Id);
            }
        }
        posDiff = 0;
        posDiff2 = 0;
        posDiff3 = 0;
        nSizeTmp = nSize;
        for ( int i = 0; i < nSize; ++i )
        {   
            avgXPos = 0;
            avgYPos = 0;
            minXPosFanout = 1000000;
            maxXPosFanout = -1000000;
            minYPosFanout = 1000000;
            maxYPosFanout = -1000000;
            nEndPoints = 0;
            nFans = 0;
            pNode = rawNodes[i];
            Abc_ObjForEachFanin( pNode, pFanin, j )
            {
                minXPosFanin = 1000000;
                maxXPosFanin = -1000000;
                minYPosFanin = 1000000;
                maxYPosFanin = -1000000;

                avgXPos += pFanin->xPos;
                avgYPos += pFanin->yPos;

                if (pFanin->rawPos)
                {
                    minXPosFanin = fminf(minXPosFanin, rectLx[pFanin->Id]);
                    maxXPosFanin = fmaxf(maxXPosFanin, rectLx[pFanin->Id]);
                    minXPosFanin = fminf(minXPosFanin, rectRx[pFanin->Id]);
                    maxXPosFanin = fmaxf(maxXPosFanin, rectRx[pFanin->Id]);
                    minYPosFanin = fminf(minYPosFanin, rectLy[pFanin->Id]);
                    maxYPosFanin = fmaxf(maxYPosFanin, rectLy[pFanin->Id]);
                    minYPosFanin = fminf(minYPosFanin, rectRy[pFanin->Id]);
                    maxYPosFanin = fmaxf(maxYPosFanin, rectRy[pFanin->Id]);
                }
                else
                {
                    minXPosFanin = fminf(minXPosFanin, pFanin->xPos);
                    maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPos);
                    minYPosFanin = fminf(minYPosFanin, pFanin->yPos);
                    maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPos);
                }

                Abc_ObjForEachFanout( pFanin, pFaninOut, k )
                {
                    // if ( pFaninOut == pNode )
                    if ( pFaninOut->rawPos )
                        continue;
                    minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
                    maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
                    minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
                    maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
                }

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
                nFans++;

                // if ( nSizeTmp < nSizeMax && !Abc_ObjIsTerm(pFanin) && !pFanin->rawPos )
                // {
                //     // rawNodes[nSizeTmp++] = pFanin;
                //     pFanin->rawPos = 1;
                // }

                // printf("%d, %d, %d&&&   ", pNode->Id, pFanin->Id, pFanin->Type);
                // printf("%d, %d ! ", pFanin->Id, pFanin->Type);
                // printf("%f, %f * ", minXPosFanin, maxXPosFanin);
                // printf("%f, %f &    ", minYPosFanin, maxYPosFanin);
            }

            // Abc_ObjForEachFanout( pNode, pFanout, j )
            //     if (pFanout->rawPos)
            //         break;
            // if ( j == Abc_ObjFanoutNum(pNode) )
            // {
            //     Abc_ObjForEachFanout( pNode, pFanout, j )
            //         if ( nSizeTmp < nSizeMax && !Abc_ObjIsTerm(pFanout) && !pFanout->rawPos )
            //         {
            //             // rawNodes[nSizeTmp++] = pFanout;
            //             pFanout->rawPos = 1;
            //         }
            // }

            Abc_ObjForEachFanout( pNode, pFanout, j )
            {
                avgXPos += pFanout->xPos;
                avgYPos += pFanout->yPos;
                nFans++;
                if ( pFanout->rawPos )
                    continue;
                minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
                maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
                minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
                maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
                // printf("%d, %d ! ", pFanout->Id, pFanout->Type);
            }

            if (minXPosFanout < 900000)
            {
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

            // printf("%f, %f ** ", minXPosFanout, maxXPosFanout);
            // printf("%f, %f &&    ", minYPosFanout, maxYPosFanout);

            if ( fHPWL )
            {   
                // posXNew = (xPosSet[nEndPoints / 2 - 1] + xPosSet[nEndPoints / 2]) / 2;
                // posYNew = (yPosSet[nEndPoints / 2 - 1] + yPosSet[nEndPoints / 2]) / 2;

                // posDiff += fabsf(posXNew - pNode->xPos) + fabsf(posYNew - pNode->yPos);
                // // printf("%f, %f *** ", posXNew, pNode->xPos);
                // // printf("%f, %f, %d, %d &&&    ", posYNew, pNode->yPos, pNode->Id, pNode->Type);
                // pNode->xPos = posXNew;
                // pNode->yPos = posYNew;


                rectLx[pNode->Id] = xPosSet[nEndPoints / 2 - 1];
                rectRx[pNode->Id] = xPosSet[nEndPoints / 2];
                rectLy[pNode->Id] = yPosSet[nEndPoints / 2 - 1];
                rectRy[pNode->Id] = yPosSet[nEndPoints / 2];
            }
            else
            {
                avgXPos /= nFans;
                avgYPos /= nFans;
                posDiff += fabsf(avgXPos - pNode->xPos) + fabsf(avgYPos - pNode->yPos);
                pNode->xPos = avgXPos;
                pNode->yPos = avgYPos;
            }
        }

        if (fHPWL)
        {   
            for ( int i = nSize - 1; i >= 0; --i )
            {   
                pNode = rawNodes[i];
                Abc_ObjForEachFanout( pNode, pFanout, j )
                    if (pFanout->rawPos)
                        break;
                if (j == Abc_ObjFanoutNum(pNode))
                {
                    posXNew = (rectLx[pNode->Id] + rectRx[pNode->Id]) / 2;
                    posYNew = (rectLy[pNode->Id] + rectRy[pNode->Id]) / 2;
                    posDiff += fabsf(posXNew - pNode->xPos) + fabsf(posYNew - pNode->yPos);
                    pNode->xPos = posXNew;
                    pNode->yPos = posYNew;
                    continue;
                }
                minXPosFanout = 1000000;
                maxXPosFanout = -1000000;
                minYPosFanout = 1000000;
                maxYPosFanout = -1000000;
                nEndPoints = 0;
                Abc_ObjForEachFanin( pNode, pFanin, j )
                {
                    minXPosFanin = 1000000;
                    maxXPosFanin = -1000000;
                    minYPosFanin = 1000000;
                    maxYPosFanin = -1000000;
                    if (pFanin->rawPos)
                    {
                        minXPosFanin = fminf(minXPosFanin, rectLx[pFanin->Id]);
                        maxXPosFanin = fmaxf(maxXPosFanin, rectLx[pFanin->Id]);
                        minXPosFanin = fminf(minXPosFanin, rectRx[pFanin->Id]);
                        maxXPosFanin = fmaxf(maxXPosFanin, rectRx[pFanin->Id]);
                        minYPosFanin = fminf(minYPosFanin, rectLy[pFanin->Id]);
                        maxYPosFanin = fmaxf(maxYPosFanin, rectLy[pFanin->Id]);
                        minYPosFanin = fminf(minYPosFanin, rectRy[pFanin->Id]);
                        maxYPosFanin = fmaxf(maxYPosFanin, rectRy[pFanin->Id]);
                    }
                    else
                    {
                        minXPosFanin = fminf(minXPosFanin, pFanin->xPos);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPos);
                        minYPosFanin = fminf(minYPosFanin, pFanin->yPos);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPos);
                    }

                    Abc_ObjForEachFanout( pFanin, pFaninOut, k )
                    {
                        // if ( pFaninOut == pNode )
                        if ( pFaninOut->rawPos )
                            continue;
                        minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
                        minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
                    }

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
                Abc_ObjForEachFanout( pNode, pFanout, j )
                {
                    minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
                    maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
                    minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
                    maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
                    // printf("%d, %d ! ", pFanout->Id, pFanout->Type);
                }

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

                posXNew = (xPosSet[nEndPoints / 2 - 1] + xPosSet[nEndPoints / 2]) / 2;
                posYNew = (yPosSet[nEndPoints / 2 - 1] + yPosSet[nEndPoints / 2]) / 2;
                posDiff += fabsf(posXNew - pNode->xPos) + fabsf(posYNew - pNode->yPos);
                pNode->xPos = posXNew;
                pNode->yPos = posYNew;
            }

            nSizeTmp = nSize;
            for ( int i = 0; i < nSize; ++i )
            {   
                pNode = rawNodes[i];
                Abc_ObjForEachFanin( pNode, pFanin, j )
                {
                    if ( nSizeTmp < nSizeMax && !Abc_ObjIsTerm(pFanin) && !pFanin->rawPos )
                    {
                        rawNodes[nSizeTmp++] = pFanin;
                        pFanin->rawPos = 2;
                    }

                    Abc_ObjForEachFanout( pFanin, pFaninOut, k )
                    {
                        if ( nSizeTmp < nSizeMax && !Abc_ObjIsTerm(pFaninOut) && !pFaninOut->rawPos )
                        {
                            rawNodes[nSizeTmp++] = pFaninOut;
                            pFaninOut->rawPos = 2;
                            // pFaninOut->rawPos = 3;
                        }
                    }
                }
                // Abc_ObjForEachFanout( pNode, pFanout, j )
                //     if (pFanout->rawPos)
                //         break;
                // if ( j == Abc_ObjFanoutNum(pNode) )
                // {
                    Abc_ObjForEachFanout( pNode, pFanout, j )
                        if ( nSizeTmp < nSizeMax && !Abc_ObjIsTerm(pFanout) && !pFanout->rawPos )
                        {
                            rawNodes[nSizeTmp++] = pFanout;
                            pFanout->rawPos = 2;
                        }
                // }
            }

            // nSizeTmp = 0;
            Abc_NtkForEachNode( pNtk, pNode, i )
            {
                if ( pNode->rawPos == 2 )
                {
                    // rawNodes[nSizeTmp++] = pNode;
                    pNode->rawPos = 0;
                }
            }
            printf("\n");
            for ( int i = nSize; i < nSizeTmp; i++ )
            {
                minXPosFanout = 1000000;
                maxXPosFanout = -1000000;
                minYPosFanout = 1000000;
                maxYPosFanout = -1000000;
                nEndPoints = 0;
                pNode = rawNodes[i];
                Abc_ObjForEachFanin( pNode, pFanin, j )
                {
                    minXPosFanin = 1000000;
                    maxXPosFanin = -1000000;
                    minYPosFanin = 1000000;
                    maxYPosFanin = -1000000;

                    minXPosFanin = fminf(minXPosFanin, pFanin->xPos);
                    maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPos);
                    minYPosFanin = fminf(minYPosFanin, pFanin->yPos);
                    maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPos);

                    Abc_ObjForEachFanout( pFanin, pFaninOut, k )
                    {
                        if ( pFaninOut == pNode )
                            continue;
                        minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
                        minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
                    }

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
                Abc_ObjForEachFanout( pNode, pFanout, j )
                {
                    minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
                    maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
                    minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
                    maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
                    // printf("%d, %d ! ", pFanout->Id, pFanout->Type);
                }
                if (minXPosFanout < 900000)
                {
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

                posXNew = (xPosSet[nEndPoints / 2 - 1] + xPosSet[nEndPoints / 2]) / 2;
                posYNew = (yPosSet[nEndPoints / 2 - 1] + yPosSet[nEndPoints / 2]) / 2;
                posDiff2 += fabsf(posXNew - pNode->xPos) + fabsf(posYNew - pNode->yPos);
                pNode->xPos = posXNew;
                pNode->yPos = posYNew;
                // printf("%d, %d & %.2f, %.2f & %.2f ** ", pNode->Id, Abc_ObjFanoutNum(pNode), pNode->xPos, pNode->yPos, posDiff2);
            }

            nSizeTmp2 = 0;
            Abc_NtkForEachNode( pNtk, pNode, i )
            {
                if ( pNode->rawPos == 3 )
                {
                    rawNodes[nSizeTmp2++] = pNode;
                    pNode->rawPos = 0;
                    // printf("%d && ", pNode->Id);
                }
            }
            printf("\n");
            for ( int i = 0; i < nSizeTmp2; i++ )
            {
                minXPosFanout = 1000000;
                maxXPosFanout = -1000000;
                minYPosFanout = 1000000;
                maxYPosFanout = -1000000;
                nEndPoints = 0;
                pNode = rawNodes[i];
                Abc_ObjForEachFanin( pNode, pFanin, j )
                {
                    minXPosFanin = 1000000;
                    maxXPosFanin = -1000000;
                    minYPosFanin = 1000000;
                    maxYPosFanin = -1000000;

                    minXPosFanin = fminf(minXPosFanin, pFanin->xPos);
                    maxXPosFanin = fmaxf(maxXPosFanin, pFanin->xPos);
                    minYPosFanin = fminf(minYPosFanin, pFanin->yPos);
                    maxYPosFanin = fmaxf(maxYPosFanin, pFanin->yPos);

                    Abc_ObjForEachFanout( pFanin, pFaninOut, k )
                    {
                        if ( pFaninOut == pNode )
                            continue;
                        minXPosFanin = fminf(minXPosFanin, pFaninOut->xPos);
                        maxXPosFanin = fmaxf(maxXPosFanin, pFaninOut->xPos);
                        minYPosFanin = fminf(minYPosFanin, pFaninOut->yPos);
                        maxYPosFanin = fmaxf(maxYPosFanin, pFaninOut->yPos);
                    }

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
                Abc_ObjForEachFanout( pNode, pFanout, j )
                {
                    minXPosFanout = fminf(minXPosFanout, pFanout->xPos);
                    maxXPosFanout = fmaxf(maxXPosFanout, pFanout->xPos);
                    minYPosFanout = fminf(minYPosFanout, pFanout->yPos);
                    maxYPosFanout = fmaxf(maxYPosFanout, pFanout->yPos);
                    // printf("%d, %d ! ", pFanout->Id, pFanout->Type);
                }

                if (minXPosFanout < 900000)
                {
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

                posXNew = (xPosSet[nEndPoints / 2 - 1] + xPosSet[nEndPoints / 2]) / 2;
                posYNew = (yPosSet[nEndPoints / 2 - 1] + yPosSet[nEndPoints / 2]) / 2;
                posDiff3 += fabsf(posXNew - pNode->xPos) + fabsf(posYNew - pNode->yPos);
                pNode->xPos = posXNew;
                pNode->yPos = posYNew;
            }
        }
        posDiff /= nSize;
        posDiff2 /= (nSizeTmp - nSize);
        posDiff3 /= nSizeTmp2;

        if (isFirst)
        {
            printf("Initial nSize = %d\n", nSize);
            printf("Initial posDiff = %f\n", posDiff);
            isFirst = 0;
        }

        // nSize = nSizeTmp;
        // nSizeMax = nSize;
        iter++;
        // Abc_Obj_t * pFanin, * pFanout;
        // int j;
        // for ( int i = 0; i < nSize; ++i )
        // {
        //     pNode = rawNodes[i];
        //     printf("%d, %d, %d, %f, %f\n", pNode->Id, pNode->Type, pNode->rawPos, pNode->xPos, pNode->yPos);
        //     Abc_ObjForEachFanin(pNode, pFanin, j)
        //         printf("%d, %d, %d, %f, %f &&& ", pFanin->Id, pFanin->Type, pFanin->rawPos, pFanin->xPos, pFanin->yPos);
        //     printf("\n");
        //     Abc_ObjForEachFanout(pNode, pFanout, j)
        //         printf("%d, %d, %d, %f, %f $$$ ", pFanout->Id, pFanout->Type, pFanout->rawPos, pFanout->xPos, pFanout->yPos);
        //     printf("\n");
        // }
        // printf("\n");
        printf("Iter = %d\n", iter);
        printf("nSize = %d\n", nSize);
        printf("nSizeTmp = %d\n", nSizeTmp);
        printf("nSizeTmp2 = %d\n", nSizeTmp2);
        printf("posDiff = %f\n", posDiff);
        printf("posDiff2 = %f\n", posDiff2);
        printf("posDiff3 = %f\n", posDiff3);
    }

    for ( int i = 0; i < nSize; ++i )
    {
        rawNodes[i]->rawPos = 0;
        // printf("%d, %d, %f, %f * ", rawNodes[i]->Id, rawNodes[i]->Type, rawNodes[i]->xPos, rawNodes[i]->yPos);
    }
    printf("\nIteration number = %d\n", iter);
    printf("Final nSize = %d\n", nSize);
    printf("Final posDiff = %f\n", posDiff);
}

int Abc_NtkRewrite2( Abc_Ntk_t * pNtk, int fUpdateLevel, int fUseZeros, int fVerbose, int fVeryVerbose, int fPlaceEnable, int fIndexStart, int fIndexEnd, int fPlace, int fDeep, int fSimple, int fClustering )
{
    extern int           Dec_GraphUpdateNetwork( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain );
    extern int           Dec_GraphUpdateNetworkP( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain, int fSimple, int fClustering );
    extern Abc_Frame_t * Abc_FrameAllocate();
    extern void Abc_FrameDeallocate( Abc_Frame_t * p );
    extern void Abc_FrameInit( Abc_Frame_t * pAbc );

    ProgressBar * pProgress;
    Cut_Man_t * pManCut;
    Rwr_Man_t * pManRwr;
    Abc_Obj_t * pNode;
//    Vec_Ptr_t * vAddedCells = NULL, * vUpdatedNets = NULL;
    Dec_Graph_t * pGraph;
    int i, nNodes, nGain, fCompl, RetValue = 1;
    float nGainHPWL;
    abctime clk, clkStart = Abc_Clock();
    Abc_Frame_t * pAbcLocal;
    if ( fDeep )
    {
        pAbcLocal = Abc_FrameAllocate();   // creates an isolated frame
        Abc_FrameInit( pAbcLocal );  //initialize the new frame
        Cmd_CommandExecute( pAbcLocal, "read_lib ../asap7lib/merged_asap7_final.lib" );
    }

    assert( Abc_NtkIsStrash(pNtk) );
    // cleanup the AIG
    Abc_AigCleanup((Abc_Aig_t *)pNtk->pManFunc);
    Abc_NtkRewritePrecomputeTiming( pNtk );
/*
    {
        Vec_Vec_t * vParts;
        vParts = Abc_NtkPartitionSmart( pNtk, 50, 1 );
        Vec_VecFree( vParts );
    }
*/

    // start placement package
//    if ( fPlaceEnable )
//    {
//        Abc_PlaceBegin( pNtk );
//        vAddedCells = Abc_AigUpdateStart( pNtk->pManFunc, &vUpdatedNets );
//    }

    // start the rewriting manager
    pManRwr = Rwr_ManStart( 0 );
    if ( pManRwr == NULL )
        return 0;
    // compute the reverse levels if level update is requested
    if ( fUpdateLevel )
        Abc_NtkStartReverseLevels( pNtk, 0 );
    // start the cut manager
clk = Abc_Clock();
    pManCut = Abc_NtkStartCutManForRewrite( pNtk );
Rwr_ManAddTimeCuts( pManRwr, Abc_Clock() - clk );
    pNtk->pManCut = pManCut;

    if ( fVeryVerbose )
        Rwr_ScoresClean( pManRwr );

    // resynthesize each node once
    pManRwr->nNodesBeg = Abc_NtkNodeNum(pNtk);
    nNodes = Abc_NtkObjNumMax(pNtk);
    pProgress = Extra_ProgressBarStart( stdout, nNodes );
    Abc_NtkForEachNode( pNtk, pNode, i )
    {   
        if ( fIndexStart != -1 && fIndexEnd != -1 && ( fIndexStart > i || fIndexEnd < i ) )
            continue;
        Extra_ProgressBarUpdate( pProgress, i, NULL );
        // stop if all nodes have been tried once
        if ( i >= nNodes )
            break;
        // skip persistant nodes
        if ( Abc_NodeIsPersistant(pNode) )
            continue;
        // skip the nodes with many fanouts
        if ( Abc_ObjFanoutNum(pNode) > 1000 )
            continue;
        // for each cut, try to resynthesize it
        if (fPlace)
        {
            nGain = Rwr_NodeRewrite( pManRwr, pManCut, pNode, fUpdateLevel, fUseZeros, fPlaceEnable );
            if ( fDeep )
                nGainHPWL = Rwr_NodeRewriteExh( pManRwr, pManCut, pNode, fUpdateLevel, fUseZeros, fPlaceEnable, pAbcLocal );
            else
                nGainHPWL = Rwr_NodeRewriteP( pManRwr, pManCut, pNode, fUpdateLevel, fUseZeros, fPlaceEnable, fSimple, fClustering );
            // printf("--------------------Statistics--------------------\n");
            // printf("total = %d, right = %d, ratio = %f\n", g_TotalComparisons, g_CorrectPredictions, (float)g_CorrectPredictions / g_TotalComparisons);
        }
        else
            nGain = Rwr_NodeRewrite( pManRwr, pManCut, pNode, fUpdateLevel, fUseZeros, fPlaceEnable );
        // printf("nGain = %d\n", nGain);
        // printf("nGainHPWL = %f\n", nGainHPWL);
        if (fPlace)
        {
            if ( !(nGainHPWL > 0 || (nGainHPWL == 0 && fUseZeros)) )
                continue;
        }
        else
        {
            if ( !(nGain > 0 || (nGain == 0 && fUseZeros)) )
                continue;
        }
        // if we end up here, a rewriting step is accepted

        // get hold of the new subgraph to be added to the AIG
        pGraph = (Dec_Graph_t *)Rwr_ManReadDecs(pManRwr);
        fCompl = Rwr_ManReadCompl(pManRwr);

        // reset the array of the changed nodes
        if ( fPlaceEnable )
            Abc_AigUpdateReset( (Abc_Aig_t *)pNtk->pManFunc );

        // complement the FF if needed
        if ( fCompl ) Dec_GraphComplement( pGraph );
clk = Abc_Clock();

// // check the global HPWL before update
// float totalHPWL = 0;
// Abc_Obj_t * pNodeAig, * pFanout;
// float xMin, xMax, yMin, yMax;
// int ii, jj;
// Abc_NtkForEachObj(pNtk, pNodeAig, ii)
// {
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
//         xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
//         yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
//         xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
//         yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
//     }
//     if ( xMin == 1000000 )
//         continue;
//     totalHPWL += xMax - xMin + yMax - yMin;
// }
// printf("totalHPWLbeforeUpdate = %f\n", totalHPWL);
        if ( fPlace && !fDeep )
        {
            // if ( !Dec_GraphUpdateNetwork( pNode, pGraph, fUpdateLevel, nGain ) )
            if ( !Dec_GraphUpdateNetworkP( pNode, pGraph, fUpdateLevel, nGain, fSimple, fClustering ) )
            {
                RetValue = -1;
                break;
            }
        }
        else
        {
            // // update with position info
            // if ( !Dec_GraphUpdateNetworkP( pNode, pGraph, fUpdateLevel, nGain, 1 ) )
            if ( !Dec_GraphUpdateNetwork( pNode, pGraph, fUpdateLevel, nGain ) )
            {
                RetValue = -1;
                break;
            }
        }
// // check the global HPWL after update
// totalHPWL = 0;
// Abc_NtkForEachObj(pNtk, pNodeAig, ii)
// {
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
//         xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
//         yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
//         xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
//         yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
//     }
//     if ( xMin == 1000000 )
//         continue;
//     totalHPWL += xMax - xMin + yMax - yMin;
// }
// printf("totalHPWLafterUpdate = %f\n", totalHPWL);
Rwr_ManAddTimeUpdate( pManRwr, Abc_Clock() - clk );
        if ( fCompl ) Dec_GraphComplement( pGraph );

        // use the array of changed nodes to update placement
//        if ( fPlaceEnable )
//            Abc_PlaceUpdate( vAddedCells, vUpdatedNets );
    }
    Extra_ProgressBarStop( pProgress );
Rwr_ManAddTimeTotal( pManRwr, Abc_Clock() - clkStart );
    // print stats
    pManRwr->nNodesEnd = Abc_NtkNodeNum(pNtk);
    if ( fVerbose )
        Rwr_ManPrintStats( pManRwr );
//        Rwr_ManPrintStatsFile( pManRwr );
    if ( fVeryVerbose )
        Rwr_ScoresReport( pManRwr );
    // delete the managers
    Rwr_ManStop( pManRwr );
    Cut_ManStop( pManCut );
    pNtk->pManCut = NULL;

    // start placement package
//    if ( fPlaceEnable )
//    {
//        Abc_PlaceEnd( pNtk );
//        Abc_AigUpdateStop( pNtk->pManFunc );
//    }

    // put the nodes into the DFS order and reassign their IDs
    {
//        abctime clk = Abc_Clock();
    Abc_NtkReassignIds( pNtk );
//        ABC_PRT( "time", Abc_Clock() - clk );
    }
//    Abc_AigCheckFaninOrder( pNtk->pManFunc );
    // fix the levels
    if ( RetValue >= 0 )
    {
        if ( fUpdateLevel )
            Abc_NtkStopReverseLevels( pNtk );
        else
            Abc_NtkLevel( pNtk );
        // check
        if ( !Abc_NtkCheck( pNtk ) )
        {
            printf( "Abc_NtkRewrite: The network check has failed.\n" );
            return 0;
        }
    }
    // free the local frame
    // Abc_FrameDeallocate(pAbcLocal);
    return RetValue;
}
int Abc_NtkRewrite( Abc_Ntk_t * pNtk, int fUpdateLevel, int fUseZeros, int fVerbose, int fVeryVerbose, int fPlaceEnable )
{
    extern int           Dec_GraphUpdateNetwork( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int fUpdateLevel, int nGain );
    ProgressBar * pProgress;
    Cut_Man_t * pManCut;
    Rwr_Man_t * pManRwr;
    Abc_Obj_t * pNode;
//    Vec_Ptr_t * vAddedCells = NULL, * vUpdatedNets = NULL;
    Dec_Graph_t * pGraph;
    int i, nNodes, nGain, fCompl, RetValue = 1;
    abctime clk, clkStart = Abc_Clock();

    assert( Abc_NtkIsStrash(pNtk) );
    // cleanup the AIG
    Abc_AigCleanup((Abc_Aig_t *)pNtk->pManFunc);
/*
    {
        Vec_Vec_t * vParts;
        vParts = Abc_NtkPartitionSmart( pNtk, 50, 1 );
        Vec_VecFree( vParts );
    }
*/

    // start placement package
//    if ( fPlaceEnable )
//    {
//        Abc_PlaceBegin( pNtk );
//        vAddedCells = Abc_AigUpdateStart( pNtk->pManFunc, &vUpdatedNets );
//    }

    // start the rewriting manager
    pManRwr = Rwr_ManStart( 0 );
    if ( pManRwr == NULL )
        return 0;
    // compute the reverse levels if level update is requested
    if ( fUpdateLevel )
        Abc_NtkStartReverseLevels( pNtk, 0 );
    // start the cut manager
clk = Abc_Clock();
    pManCut = Abc_NtkStartCutManForRewrite( pNtk );
Rwr_ManAddTimeCuts( pManRwr, Abc_Clock() - clk );
    pNtk->pManCut = pManCut;

    if ( fVeryVerbose )
        Rwr_ScoresClean( pManRwr );

    // resynthesize each node once
    pManRwr->nNodesBeg = Abc_NtkNodeNum(pNtk);
    nNodes = Abc_NtkObjNumMax(pNtk);
    pProgress = Extra_ProgressBarStart( stdout, nNodes );
    Abc_NtkForEachNode( pNtk, pNode, i )
    {
        Extra_ProgressBarUpdate( pProgress, i, NULL );
        // stop if all nodes have been tried once
        if ( i >= nNodes )
            break;
        // skip persistant nodes
        if ( Abc_NodeIsPersistant(pNode) )
            continue;
        // skip the nodes with many fanouts
        if ( Abc_ObjFanoutNum(pNode) > 1000 )
            continue;

        // for each cut, try to resynthesize it
        nGain = Rwr_NodeRewrite( pManRwr, pManCut, pNode, fUpdateLevel, fUseZeros, fPlaceEnable );
        if ( !(nGain > 0 || (nGain == 0 && fUseZeros)) )
            continue;
        // if we end up here, a rewriting step is accepted

        // get hold of the new subgraph to be added to the AIG
        pGraph = (Dec_Graph_t *)Rwr_ManReadDecs(pManRwr);
        fCompl = Rwr_ManReadCompl(pManRwr);

        // reset the array of the changed nodes
        if ( fPlaceEnable )
            Abc_AigUpdateReset( (Abc_Aig_t *)pNtk->pManFunc );

        // complement the FF if needed
        if ( fCompl ) Dec_GraphComplement( pGraph );
clk = Abc_Clock();
        if ( !Dec_GraphUpdateNetwork( pNode, pGraph, fUpdateLevel, nGain ) )
        {
            RetValue = -1;
            break;
        }
Rwr_ManAddTimeUpdate( pManRwr, Abc_Clock() - clk );
        if ( fCompl ) Dec_GraphComplement( pGraph );

        // use the array of changed nodes to update placement
//        if ( fPlaceEnable )
//            Abc_PlaceUpdate( vAddedCells, vUpdatedNets );
    }
    Extra_ProgressBarStop( pProgress );
Rwr_ManAddTimeTotal( pManRwr, Abc_Clock() - clkStart );
    // print stats
    pManRwr->nNodesEnd = Abc_NtkNodeNum(pNtk);
    if ( fVerbose )
        Rwr_ManPrintStats( pManRwr );
//        Rwr_ManPrintStatsFile( pManRwr );
    if ( fVeryVerbose )
        Rwr_ScoresReport( pManRwr );
    // delete the managers
    Rwr_ManStop( pManRwr );
    Cut_ManStop( pManCut );
    pNtk->pManCut = NULL;

    // start placement package
//    if ( fPlaceEnable )
//    {
//        Abc_PlaceEnd( pNtk );
//        Abc_AigUpdateStop( pNtk->pManFunc );
//    }

    // put the nodes into the DFS order and reassign their IDs
    {
//        abctime clk = Abc_Clock();
    Abc_NtkReassignIds( pNtk );
//        ABC_PRT( "time", Abc_Clock() - clk );
    }
//    Abc_AigCheckFaninOrder( pNtk->pManFunc );
    // fix the levels
    if ( RetValue >= 0 )
    {
        if ( fUpdateLevel )
            Abc_NtkStopReverseLevels( pNtk );
        else
            Abc_NtkLevel( pNtk );
        // check
        if ( !Abc_NtkCheck( pNtk ) )
        {
            printf( "Abc_NtkRewrite: The network check has failed.\n" );
            return 0;
        }
    }
    return RetValue;
}


/**Function*************************************************************

  Synopsis    [Starts the cut manager for rewriting.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Cut_Man_t * Abc_NtkStartCutManForRewrite( Abc_Ntk_t * pNtk )
{
    static Cut_Params_t Params, * pParams = &Params;
    Cut_Man_t * pManCut;
    Abc_Obj_t * pObj;
    int i;
    // start the cut manager
    memset( pParams, 0, sizeof(Cut_Params_t) );
    pParams->nVarsMax  = 4;     // the max cut size ("k" of the k-feasible cuts)
    pParams->nKeepMax  = 250;   // the max number of cuts kept at a node
    pParams->fTruth    = 1;     // compute truth tables
    pParams->fFilter   = 1;     // filter dominated cuts
    pParams->fSeq      = 0;     // compute sequential cuts
    pParams->fDrop     = 0;     // drop cuts on the fly
    pParams->fVerbose  = 0;     // the verbosiness flag
    pParams->nIdsMax   = Abc_NtkObjNumMax( pNtk );
    pManCut = Cut_ManStart( pParams );
    if ( pParams->fDrop )
        Cut_ManSetFanoutCounts( pManCut, Abc_NtkFanoutCounts(pNtk) );
    // set cuts for PIs
    Abc_NtkForEachCi( pNtk, pObj, i )
        if ( Abc_ObjFanoutNum(pObj) > 0 )
            Cut_NodeSetTriv( pManCut, pObj->Id );
    return pManCut;
}

/**Function*************************************************************

  Synopsis    [Prints the cuts at the nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NodePrintCuts( Abc_Obj_t * pNode )
{
    Vec_Ptr_t * vCuts;
    Cut_Cut_t * pCut;
    int k;

    printf( "\nNode %s\n", Abc_ObjName(pNode) );
    vCuts = (Vec_Ptr_t *)pNode->pCopy;
    Vec_PtrForEachEntry( Cut_Cut_t *, vCuts, pCut, k )
    {
        Extra_PrintBinary( stdout, (unsigned *)&pCut->uSign, 16 ); 
        printf( "   " );
        Cut_CutPrint( pCut, 0 );   
        printf( "\n" );
    }
}


/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_ManRewritePrintDivs( Vec_Ptr_t * vDivs, int nLeaves )
{
    Abc_Obj_t * pFanin, * pNode, * pRoot;
    int i, k;
    pRoot = (Abc_Obj_t *)Vec_PtrEntryLast(vDivs);
    // print the nodes
    Vec_PtrForEachEntry( Abc_Obj_t *, vDivs, pNode, i )
    {
        if ( i < nLeaves )
        {
            printf( "%6d : %c\n", pNode->Id, 'a'+i );
            continue;
        }
        printf( "%6d : %2d = ", pNode->Id, i );
        // find the first fanin
        Vec_PtrForEachEntry( Abc_Obj_t *, vDivs, pFanin, k )
            if ( Abc_ObjFanin0(pNode) == pFanin )
                break;
        if ( k < nLeaves )
            printf( "%c", 'a' + k );
        else
            printf( "%d", k );
        printf( "%s ", Abc_ObjFaninC0(pNode)? "\'" : "" );
        // find the second fanin
        Vec_PtrForEachEntry( Abc_Obj_t *, vDivs, pFanin, k )
            if ( Abc_ObjFanin1(pNode) == pFanin )
                break;
        if ( k < nLeaves )
            printf( "%c", 'a' + k );
        else
            printf( "%d", k );
        printf( "%s ", Abc_ObjFaninC1(pNode)? "\'" : "" );
        if ( pNode == pRoot )
            printf( " root" );
        printf( "\n" );
    }
    printf( "\n" );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_ManShowCutCone_rec( Abc_Obj_t * pNode, Vec_Ptr_t * vDivs )
{
    if ( Abc_NodeIsTravIdCurrent(pNode) )
        return;
    Abc_NodeSetTravIdCurrent(pNode);
    Abc_ManShowCutCone_rec( Abc_ObjFanin0(pNode), vDivs );
    Abc_ManShowCutCone_rec( Abc_ObjFanin1(pNode), vDivs );
    Vec_PtrPush( vDivs, pNode );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_ManShowCutCone( Abc_Obj_t * pNode, Vec_Ptr_t * vLeaves )
{
    Abc_Ntk_t * pNtk = pNode->pNtk;
    Abc_Obj_t * pObj;
    Vec_Ptr_t * vDivs;
    int i;
    vDivs = Vec_PtrAlloc( 100 );
    Abc_NtkIncrementTravId( pNtk );
    Vec_PtrForEachEntry( Abc_Obj_t *, vLeaves, pObj, i )
    {
        Abc_NodeSetTravIdCurrent( Abc_ObjRegular(pObj) );
        Vec_PtrPush( vDivs, Abc_ObjRegular(pObj) );
    }
    Abc_ManShowCutCone_rec( pNode, vDivs );
    Abc_ManRewritePrintDivs( vDivs, Vec_PtrSize(vLeaves) );
    Vec_PtrFree( vDivs );
}


/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_RwrExpWithCut_rec( Abc_Obj_t * pNode, Vec_Ptr_t * vLeaves, int fUseA )
{
    if ( Vec_PtrFind(vLeaves, pNode) >= 0 || Vec_PtrFind(vLeaves, Abc_ObjNot(pNode)) >= 0 )
    {
        if ( fUseA )
            Abc_ObjRegular(pNode)->fMarkA = 1;
        else
            Abc_ObjRegular(pNode)->fMarkB = 1;
        return;
    }
    assert( Abc_ObjIsNode(pNode) );
    Abc_RwrExpWithCut_rec( Abc_ObjFanin0(pNode), vLeaves, fUseA );
    Abc_RwrExpWithCut_rec( Abc_ObjFanin1(pNode), vLeaves, fUseA );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_RwrExpWithCut( Abc_Obj_t * pNode, Vec_Ptr_t * vLeaves )
{
    Abc_Obj_t * pObj;
    int i, CountA, CountB;
    Abc_RwrExpWithCut_rec( Abc_ObjFanin0(pNode), vLeaves, 1 );
    Abc_RwrExpWithCut_rec( Abc_ObjFanin1(pNode), vLeaves, 0 );
    CountA = CountB = 0;
    Vec_PtrForEachEntry( Abc_Obj_t *, vLeaves, pObj, i )
    {
        CountA += Abc_ObjRegular(pObj)->fMarkA;
        CountB += Abc_ObjRegular(pObj)->fMarkB;
        Abc_ObjRegular(pObj)->fMarkA = 0;
        Abc_ObjRegular(pObj)->fMarkB = 0;
    }
    printf( "(%d,%d:%d) ", CountA, CountB, CountA+CountB-Vec_PtrSize(vLeaves) );
}


////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END
