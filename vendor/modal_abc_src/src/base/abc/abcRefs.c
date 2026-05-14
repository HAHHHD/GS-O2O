/**CFile****************************************************************

  FileName    [abcRefs.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Network and node package.]

  Synopsis    [Procedures using reference counting of the AIG nodes.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: abcRefs.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "abc.h"
#include "math.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

static int Abc_NodeRefDeref( Abc_Obj_t * pNode, int fReference, int fLabel );
static float Abc_NodeRefDerefHPWL( Abc_Obj_t * pNode, int fReference, int fLabel, int fClustering );
static int Abc_NodeRefDerefStop( Abc_Obj_t * pNode, int fReference );

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Returns the MFFC size.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeMffcSize( Abc_Obj_t * pNode )
{
    int nConeSize1, nConeSize2;
//    assert( Abc_NtkIsStrash(pNode->pNtk) );
//    assert( !Abc_ObjIsComplement( pNode ) );
    assert( Abc_ObjIsNode( pNode ) );
    if ( Abc_ObjFaninNum(pNode) == 0 )
        return 0;
    nConeSize1 = Abc_NodeRefDeref( pNode, 0, 0 ); // dereference
    nConeSize2 = Abc_NodeRefDeref( pNode, 1, 0 ); // reference
    assert( nConeSize1 == nConeSize2 );
    assert( nConeSize1 > 0 );
    return nConeSize1;
}

/**Function*************************************************************

  Synopsis    [Returns the MFFC size while stopping at the complemented edges.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeMffcSizeStop( Abc_Obj_t * pNode )
{
    int nConeSize1, nConeSize2;
    assert( Abc_NtkIsStrash(pNode->pNtk) );
    assert( !Abc_ObjIsComplement( pNode ) );
    assert( Abc_ObjIsNode( pNode ) );
    if ( Abc_ObjFaninNum(pNode) == 0 )
        return 0;
    nConeSize1 = Abc_NodeRefDerefStop( pNode, 0 ); // dereference
    nConeSize2 = Abc_NodeRefDerefStop( pNode, 1 ); // reference
    assert( nConeSize1 == nConeSize2 );
    assert( nConeSize1 > 0 );
    return nConeSize1;
}

/**Function*************************************************************

  Synopsis    [Labels MFFC with the current traversal ID.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeMffcLabelAig( Abc_Obj_t * pNode )
{
    int nConeSize1, nConeSize2;
    assert( Abc_NtkIsStrash(pNode->pNtk) );
    assert( !Abc_ObjIsComplement( pNode ) );
    assert( Abc_ObjIsNode( pNode ) );
    if ( Abc_ObjFaninNum(pNode) == 0 )
        return 0;
    nConeSize1 = Abc_NodeRefDeref( pNode, 0, 1 ); // dereference
    nConeSize2 = Abc_NodeRefDeref( pNode, 1, 0 ); // reference
    assert( nConeSize1 == nConeSize2 );
    assert( nConeSize1 > 0 );
    return nConeSize1;
}


float Abc_NodeMffcLabelAigHPWL( Abc_Obj_t * pNode, float * overflowSaved, int fClustering )
{
    float nConeSize1;
    float xMin, xMax, yMin, yMax;
    Abc_Obj_t * pFanout;
    int i;
    assert( Abc_NtkIsStrash(pNode->pNtk) );
    assert( !Abc_ObjIsComplement( pNode ) );
    assert( Abc_ObjIsNode( pNode ) );
    if ( Abc_ObjFaninNum(pNode) == 0 )
        return 0;

    pNode->pNtk->nCellsPerCut = 0;
    Vec_IntClear( &pNode->pNtk->vNodesPerCell );
    Vec_FltClear( &pNode->pNtk->vXPosCell );
    Vec_FltClear( &pNode->pNtk->vYPosCell );
    
    xMin = xMax = pNode->xPos;
    yMin = yMax = pNode->yPos;
    // printf("*%d, %d, %2f, %2f\n", pNode->Id, pNode->Type, pNode->xPos, pNode->yPos);
    Abc_ObjForEachFanout( pNode, pFanout, i )
    {
        // IO floating mode: skip the terminals ***************************
        if ( Abc_ObjIsTerm(pFanout) )
            continue;
        // printf("&%d, %d, %2f, %2f\n", pFanout->Id, pFanout->Type, pFanout->xPos, pFanout->yPos);
        if (pFanout->Id == 0)
            continue;
        xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
        yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
        xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
        yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
    }
    nConeSize1 = xMax - xMin + yMax - yMin;
    *overflowSaved = 0;
    nConeSize1 += Abc_NodeRefDerefHPWL( pNode, 0, 1, fClustering ); // dereference
    *overflowSaved += Abc_NodeRefDerefHPWL( pNode, 1, 0, fClustering ); // reference
    // printf("nConeSize1 = %.2f, nConeSize2 = %.2f\n", nConeSize1, nConeSize2);
    // assert( nConeSize1 > 0 );
    return nConeSize1;
}
/**Function*************************************************************

  Synopsis    [References/references the node and returns MFFC size.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeRefDeref( Abc_Obj_t * pNode, int fReference, int fLabel )
{
    Abc_Obj_t * pNode0, * pNode1;
    int Counter;
    // Abc_Obj_t * pFanout;
    // int i;
    // label visited nodes
    if ( fLabel )
        Abc_NodeSetTravIdCurrent( pNode );
    // skip the CI
    if ( Abc_ObjIsCi(pNode) )
        return 0;
    // process the internal node
    pNode0 = Abc_ObjFanin0(pNode);
    pNode1 = Abc_ObjFanin1(pNode);
    Counter = 1;
    if ( fReference )
    {
        if ( pNode0->vFanouts.nSize++ == 0 )
            Counter += Abc_NodeRefDeref( pNode0, fReference, fLabel );
        if ( pNode1->vFanouts.nSize++ == 0 )
            Counter += Abc_NodeRefDeref( pNode1, fReference, fLabel );
    }
    else
    {
        assert( pNode0->vFanouts.nSize > 0 );
        assert( pNode1->vFanouts.nSize > 0 );
        // printf("%d *0* %d\n", pNode0->vFanouts.nSize, pNode->Id);
        // Abc_ObjForEachFanout(pNode0, pFanout, i)
        // {
        //     if ( pNode0->marked && i == pNode0->vFanouts.nSize - 1 )
        //         continue;
        //     if (i >= pNode0->vFanouts.nSize - 2)
        //         printf("%d, %d, %d\n", i, pFanout->Id, pFanout->Type);
        // }
        if ( --pNode0->vFanouts.nSize == 0 )
            Counter += Abc_NodeRefDeref( pNode0, fReference, fLabel );
        if ( --pNode1->vFanouts.nSize == 0 )
            Counter += Abc_NodeRefDeref( pNode1, fReference, fLabel );
    }
    return Counter;
}



float Abc_NodeRefDerefHPWL( Abc_Obj_t * pNode, int fReference, int fLabel, int fClustering )
{
    Abc_Obj_t * pNode0, * pNode1;
    Abc_Obj_t * pFanout;
    Abc_Ntk_t * pNtk = pNode->pNtk;
    int i, nFanout;
    float diffHPWL = 0.0;
    float xMin, xMax, yMin, yMax;
    int xIdx, yIdx;
    float xOff, yOff;
    float halfBinRatio = pNtk->halfBinRatio;
    // float xMin, yMin, xMax, yMax;
    int xb0, xb1, yb0, yb1;
    float x0, x1, y0, y1;
    float xShare[10], yShare[10];
    // label visited nodes
    if ( fLabel )
        Abc_NodeSetTravIdCurrent( pNode );
    // skip the CI
    if ( Abc_ObjIsCi(pNode) )
        return 0;
    // process the internal node
    pNode0 = Abc_ObjFanin0(pNode);
    pNode1 = Abc_ObjFanin1(pNode);
    if ( fReference )
    {
        if ( fClustering )
        {
            // cluster AIG nodes in each cell
            int k;
            for (k = 0; k < pNtk->nCellsPerCut; k++)
                if (fabsf(Vec_FltEntry(&pNtk->vXPosCell, k) - pNode->xPos) < 0.001 &&
                    fabsf(Vec_FltEntry(&pNtk->vYPosCell, k) - pNode->yPos) < 0.001)
                    break;
            // a new cell, initialization
            if (k == pNtk->nCellsPerCut)
            {
                Vec_IntPush( &pNtk->vNodesPerCell, 1 );
                Vec_FltPush( &pNtk->vXPosCell, pNode->xPos );
                Vec_FltPush( &pNtk->vYPosCell, pNode->yPos );
                pNtk->nCellsPerCut++;
            }
            // add to the current cluster
            else
                Vec_IntAddToEntry( &pNtk->vNodesPerCell, k, 1 );
        }
        
        // binDensity change
        assert( pNtk->nPosTmp < 1000 );
        pNtk->idTmp[pNtk->nPosTmp++] = pNode->Id;
        xMin = pNode->xPos - pNode->half_den_sizeX;
        yMin = pNode->yPos - pNode->half_den_sizeY;
        xMax = pNode->xPos + pNode->half_den_sizeX;
        yMax = pNode->yPos + pNode->half_den_sizeY;
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
        assert( xb1 - xb0 < 10 );
        assert( yb1 - yb0 < 10 );
        
        for( int xIdx = xb0; xIdx <= xb1; xIdx++) {
            x0 = xIdx * pNtk->binStepX + pNtk->binOriX;

            x0 = fmaxf(x0, xMin);

            x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;

            x1 = fminf(x1, xMax);

            assert( xIdx - xb0 < 10 );
            xShare[xIdx - xb0] = x1 - x0;
        }

        for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
            y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

            y0 = fmaxf(y0, yMin);

            y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

            y1 = fminf(y1, yMax);

            assert( yIdx - yb0 < 10 );
            yShare[yIdx - yb0] = y1 - y0;
        }

        for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
            for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                // diffHPWL += fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] - xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNode->den_scal)
                //             - fmaxf(1, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
                pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] -= xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNode->den_scal;
                // if (pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] < 0) 
                //     printf("%d, %d, %f, %f, %f, %f, %f, %f, %f\n", xIdx, yIdx, pNtk->binStepX, pNtk->binStepY, xShare[xIdx - xb0], yShare[yIdx - yb0], pNtk->invBinArea, pNode->den_scal, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx]);
            }
        }

        if ( pNode0->vFanouts.nSize++ == 0 )
            diffHPWL += Abc_NodeRefDerefHPWL( pNode0, fReference, fLabel, fClustering );
        if ( pNode1->vFanouts.nSize++ == 0 )
            diffHPWL += Abc_NodeRefDerefHPWL( pNode1, fReference, fLabel, fClustering );
    }
    else
    {
        assert( pNode0->vFanouts.nSize > 0 );
        assert( pNode1->vFanouts.nSize > 0 );
        // if pNode0 is a Ci, ignore the wire
        if ( Abc_ObjIsTerm(pNode0) )
        {
            xMin = yMin = 1000000;
            xMax = yMax = -1000000;
        }
        else
        {
            xMin = xMax = pNode0->xPos;
            yMin = yMax = pNode0->yPos;
        }
        nFanout = Abc_ObjFanoutNum(pNode0) - 1;
        if ( pNode0->marked )
            nFanout--;
        for ( i = 0; (i < nFanout) && (((pFanout) = Abc_ObjFanout(pNode0, i)), 1); i++ )
        {   
            // IO floating mode: skip the terminals ***************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            // printf("%d, %d, %d*0*\n", pNode0->Id, nFanout, i);
            // printf("%d*00*\n", pNode0->vFanouts.pArray[i]);
            if ( Abc_NodeIsTravIdCurrent(pFanout) )
            {   
                // printf("%d*0skip0*\n", pFanout->Id);
                nFanout++;
                continue;
            }
            if (pFanout->Id == 0)
                continue;
            xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
        }
        if ( xMin != 1000000 )
        {
            if ( pNode->xPos < xMin )
                diffHPWL += xMin - pNode->xPos;
            if ( pNode->xPos > xMax )
                diffHPWL += pNode->xPos - xMax;
            if ( pNode->yPos < yMin )
                diffHPWL += yMin - pNode->yPos;
            if ( pNode->yPos > yMax )
                diffHPWL += pNode->yPos - yMax;
        }
        // HPWL reduction after removing the edge between pNode and pNode1
        // if pNode1 is a Ci, ignore the wire
        if ( Abc_ObjIsTerm(pNode1) )
        {
            xMin = yMin = 1000000;
            xMax = yMax = -1000000;
        }
        else
        {
            xMin = xMax = pNode1->xPos;
            yMin = yMax = pNode1->yPos;
        }
        nFanout = Abc_ObjFanoutNum(pNode1) - 1;
        if ( pNode1->marked )
            nFanout--;
        for ( i = 0; (i < nFanout) && (((pFanout) = Abc_ObjFanout(pNode1, i)), 1); i++ )
        {   
            // IO floating mode: skip the terminals ***************************
            if ( Abc_ObjIsTerm(pFanout) )
                continue;
            // printf("%d, %d, %d *1*\n", pNode1->Id, nFanout, i);
            if ( Abc_NodeIsTravIdCurrent(pFanout) )
            {   
                nFanout++;
                continue;
            }
            if (pFanout->Id == 0)
                continue;
            xMin = ( pFanout->xPos < xMin ) ? pFanout->xPos : xMin;
            yMin = ( pFanout->yPos < yMin ) ? pFanout->yPos : yMin;
            xMax = ( pFanout->xPos > xMax ) ? pFanout->xPos : xMax;
            yMax = ( pFanout->yPos > yMax ) ? pFanout->yPos : yMax;
        }
        if ( xMin != 1000000 )
        {
            if ( pNode->xPos < xMin )
                diffHPWL += xMin - pNode->xPos;
            if ( pNode->xPos > xMax )
                diffHPWL += pNode->xPos - xMax;
            if ( pNode->yPos < yMin )
                diffHPWL += yMin - pNode->yPos;
            if ( pNode->yPos > yMax )
                diffHPWL += pNode->yPos - yMax;
        }

        int vFanouts1Copy = --pNode1->vFanouts.nSize;
        if ( --pNode0->vFanouts.nSize == 0 )
            diffHPWL += Abc_NodeRefDerefHPWL( pNode0, fReference, fLabel, fClustering );
        if ( vFanouts1Copy == 0 )
            diffHPWL += Abc_NodeRefDerefHPWL( pNode1, fReference, fLabel, fClustering );
    }
    return diffHPWL;
}
/**Function*************************************************************

  Synopsis    [References/references the node and returns MFFC size.]

  Description [Stops at the complemented edges.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeRefDerefStop( Abc_Obj_t * pNode, int fReference )
{
    Abc_Obj_t * pNode0, * pNode1;
    int Counter;
    // skip the CI
    if ( Abc_ObjIsCi(pNode) )
        return 0;
    // process the internal node
    pNode0 = Abc_ObjFanin0(pNode);
    pNode1 = Abc_ObjFanin1(pNode);
    Counter = 1;
    if ( fReference )
    {
        if ( !Abc_ObjFaninC0(pNode) && pNode0->vFanouts.nSize++ == 0 )
            Counter += Abc_NodeRefDerefStop( pNode0, fReference );
        if ( !Abc_ObjFaninC1(pNode) && pNode1->vFanouts.nSize++ == 0 )
            Counter += Abc_NodeRefDerefStop( pNode1, fReference );
    }
    else
    {
        assert( pNode0->vFanouts.nSize > 0 );
        assert( pNode1->vFanouts.nSize > 0 );
        if ( !Abc_ObjFaninC0(pNode) && --pNode0->vFanouts.nSize == 0 )
            Counter += Abc_NodeRefDerefStop( pNode0, fReference );
        if ( !Abc_ObjFaninC1(pNode) && --pNode1->vFanouts.nSize == 0 )
            Counter += Abc_NodeRefDerefStop( pNode1, fReference );
    }
    return Counter;
}




/**Function*************************************************************

  Synopsis    [Dereferences the node's MFFC.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeDeref_rec( Abc_Obj_t * pNode )
{
    Abc_Obj_t * pFanin;
    int i, Counter = 1;
    if ( Abc_ObjIsCi(pNode) )
        return 0;
    Abc_ObjForEachFanin( pNode, pFanin, i )
    {
        assert( pFanin->vFanouts.nSize > 0 );
        if ( --pFanin->vFanouts.nSize == 0 )
            Counter += Abc_NodeDeref_rec( pFanin );
    }
    return Counter;
}

/**Function*************************************************************

  Synopsis    [References the node's MFFC.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NodeRef_rec( Abc_Obj_t * pNode )
{
    Abc_Obj_t * pFanin;
    int i, Counter = 1;
    if ( Abc_ObjIsCi(pNode) )
        return 0;
    Abc_ObjForEachFanin( pNode, pFanin, i )
    {
        if ( pFanin->vFanouts.nSize++ == 0 )
            Counter += Abc_NodeRef_rec( pFanin );
    }
    return Counter;
}

/**Function*************************************************************

  Synopsis    [Collects the internal and boundary nodes in the derefed MFFC.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NodeMffcConeSupp_rec( Abc_Obj_t * pNode, Vec_Ptr_t * vCone, Vec_Ptr_t * vSupp, int fTopmost )
{
    Abc_Obj_t * pFanin;
    int i;
    // skip visited nodes
    if ( Abc_NodeIsTravIdCurrent(pNode) )
        return;
    Abc_NodeSetTravIdCurrent(pNode);
    // add to the new support nodes
    if ( !fTopmost && (Abc_ObjIsCi(pNode) || pNode->vFanouts.nSize > 0) )
    {
        if ( vSupp ) Vec_PtrPush( vSupp, pNode );
        return;
    }
    // recur on the children
    Abc_ObjForEachFanin( pNode, pFanin, i )
        Abc_NodeMffcConeSupp_rec( pFanin, vCone, vSupp, 0 );
    // collect the internal node
    if ( vCone ) Vec_PtrPush( vCone, pNode );
//    printf( "%d ", pNode->Id );
}

/**Function*************************************************************

  Synopsis    [Collects the support of the derefed MFFC.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NodeMffcConeSupp( Abc_Obj_t * pNode, Vec_Ptr_t * vCone, Vec_Ptr_t * vSupp )
{
    assert( Abc_ObjIsNode(pNode) );
    assert( !Abc_ObjIsComplement(pNode) );
    if ( vCone ) Vec_PtrClear( vCone );
    if ( vSupp ) Vec_PtrClear( vSupp );
    Abc_NtkIncrementTravId( pNode->pNtk );
    Abc_NodeMffcConeSupp_rec( pNode, vCone, vSupp, 1 );
//    printf( "\n" );
}

/**Function*************************************************************

  Synopsis    [Collects the support of the derefed MFFC.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NodeMffcConeSuppPrint( Abc_Obj_t * pNode )
{
    Vec_Ptr_t * vCone, * vSupp;
    Abc_Obj_t * pObj;
    int i;
    vCone = Vec_PtrAlloc( 100 );
    vSupp = Vec_PtrAlloc( 100 );
    Abc_NodeDeref_rec( pNode );
    Abc_NodeMffcConeSupp( pNode, vCone, vSupp );
    Abc_NodeRef_rec( pNode );
    printf( "Node = %6s : Supp = %3d  Cone = %3d  (", 
        Abc_ObjName(pNode), Vec_PtrSize(vSupp), Vec_PtrSize(vCone) );
    Vec_PtrForEachEntry( Abc_Obj_t *, vCone, pObj, i )
        printf( " %s", Abc_ObjName(pObj) );
    printf( " )\n" );
    Vec_PtrFree( vCone );
    Vec_PtrFree( vSupp );
}

/**Function*************************************************************

  Synopsis    [Collects the internal nodes of the MFFC limited by cut.]

  Description []
               
  SideEffects [Increments the trav ID and marks visited nodes.]

  SeeAlso     []

***********************************************************************/
int Abc_NodeMffcInside( Abc_Obj_t * pNode, Vec_Ptr_t * vLeaves, Vec_Ptr_t * vInside )
{
    Abc_Obj_t * pObj;
    int i, Count1, Count2;
    // increment the fanout counters for the leaves
    Vec_PtrForEachEntry( Abc_Obj_t *, vLeaves, pObj, i )
        pObj->vFanouts.nSize++;
    // dereference the node
    Count1 = Abc_NodeDeref_rec( pNode );
    // collect the nodes inside the MFFC
    Abc_NodeMffcConeSupp( pNode, vInside, NULL );
    // reference it back
    Count2 = Abc_NodeRef_rec( pNode );
    assert( Count1 == Count2 );
    // remove the extra counters
    Vec_PtrForEachEntry( Abc_Obj_t *, vLeaves, pObj, i )
        pObj->vFanouts.nSize--;
    return Count1;
}

/**Function*************************************************************

  Synopsis    [Collects the internal nodes of the MFFC limited by cut.]

  Description []
               
  SideEffects [Increments the trav ID and marks visited nodes.]

  SeeAlso     []

***********************************************************************/
Vec_Ptr_t * Abc_NodeMffcInsideCollect( Abc_Obj_t * pNode )
{
    Vec_Ptr_t * vInside;
    int Count1, Count2;
    // dereference the node
    Count1 = Abc_NodeDeref_rec( pNode );
    // collect the nodes inside the MFFC
    vInside = Vec_PtrAlloc( 10 );
    Abc_NodeMffcConeSupp( pNode, vInside, NULL );
    // reference it back
    Count2 = Abc_NodeRef_rec( pNode );
    assert( Count1 == Count2 );
    return vInside;
}

/**Function*************************************************************

  Synopsis    [Collects the internal and boundary nodes in the derefed MFFC.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NodeMffcLabel_rec( Abc_Obj_t * pNode, int fTopmost, Vec_Ptr_t * vNodes )
{
    Abc_Obj_t * pFanin;
    int i;
    // add to the new support nodes
    if ( !fTopmost && (Abc_ObjIsCi(pNode) || pNode->vFanouts.nSize > 0) )
        return;
    // skip visited nodes
    if ( Abc_NodeIsTravIdCurrent(pNode) )
        return;
    Abc_NodeSetTravIdCurrent(pNode);
    // recur on the children
    Abc_ObjForEachFanin( pNode, pFanin, i )
        Abc_NodeMffcLabel_rec( pFanin, 0, vNodes );
    // collect the internal node
//    printf( "%d ", pNode->Id );
    if ( vNodes )
        Vec_PtrPush( vNodes, pNode );
}

/**Function*************************************************************

  Synopsis    [Collects the internal nodes of the MFFC limited by cut.]

  Description []
               
  SideEffects [Increments the trav ID and marks visited nodes.]

  SeeAlso     []

***********************************************************************/
int Abc_NodeMffcLabel( Abc_Obj_t * pNode, Vec_Ptr_t * vNodes )
{
    int Count1, Count2;
    // dereference the node
    Count1 = Abc_NodeDeref_rec( pNode );
    // collect the nodes inside the MFFC
    Abc_NtkIncrementTravId( pNode->pNtk );
    Abc_NodeMffcLabel_rec( pNode, 1, vNodes );
    // reference it back
    Count2 = Abc_NodeRef_rec( pNode );
    assert( Count1 == Count2 );
    return Count1;
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END
