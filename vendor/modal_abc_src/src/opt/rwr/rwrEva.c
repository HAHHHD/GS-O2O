/**CFile****************************************************************

  FileName    [rwrDec.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [DAG-aware AIG rewriting package.]

  Synopsis    [Evaluation and decomposition procedures.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: rwrDec.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "rwr.h"
#include "bool/dec/dec.h"
#include "aig/ivy/ivy.h"

#include "math.h"
#include "stdlib.h"
#include "time.h"

ABC_NAMESPACE_IMPL_START

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define GNN_PORT 65432

int g_TotalComparisons = 0;
int g_CorrectPredictions = 0;

float Abc_CallGNN(Abc_Ntk_t * pNtk) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    float score = -9999.0;
    char * temp_filename = "temp_gnn_query.aig"; 

    // 1. Serialize Network to AIGER in memory/file
    Io_Write(pNtk, temp_filename, IO_FILE_AIGER);

    // 2. Read into buffer
    FILE *f = fopen(temp_filename, "rb");
    if (!f) return score;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    char * buffer = (char *)malloc(fsize);
    fread(buffer, 1, fsize, f);
    fclose(f);

    // 3. Connect to Python Server
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        free(buffer); return score;
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(GNN_PORT);
    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        free(buffer); close(sock); return score;
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection Failed. Is gnn_score.py running?\n");
        free(buffer); close(sock); return score;
    }

    // 4. Send Size (4 bytes) then Data
    uint32_t net_size = htonl((uint32_t)fsize);
    send(sock, &net_size, sizeof(uint32_t), 0);
    send(sock, buffer, fsize, 0);

    // 5. Receive Score
    read(sock, &score, sizeof(float));

    free(buffer);
    close(sock);
    return score;
}

// #include <stdio.h>
// #include <stdlib.h>

// // Helper: Writes network to disk, calls python script, reads score
// float Abc_CallGNN( Abc_Ntk_t * pNtk )
// {
//     char cmd[1024];
//     char resultBuff[128];
//     float score = -9999.0; // Default error value
    
//     // 1. Write current network state to a temp file
//     // Note: IO_FILE_AIGER is usually defined in "base/io/io.h"
//     char * tempFile = "temp_gnn_query.aig";
//     Io_Write( pNtk, tempFile, IO_FILE_AIGER );

//     // 2. Construct command to call the python script
//     // Ensure 'gnn_score.py' and 'best_model.pth' are in the ABC run directory
//     sprintf(cmd, "/opt/miniconda3/envs/281A/bin/python gnn_score.py %s", tempFile);

//     // 3. Execute Python script and read stdout
//     FILE * fp = popen( cmd, "r" );
//     if ( fp == NULL ) {
//         printf("Error: Failed to run gnn_score.py\n");
//         return score;
//     }

//     // 4. Parse the float score from the first line of output
//     if ( fgets( resultBuff, sizeof(resultBuff), fp ) != NULL ) {
//         score = (float)atof( resultBuff );
//     }

//     pclose( fp );
//     return score;
// }


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

static Dec_Graph_t * Rwr_CutEvaluate( Rwr_Man_t * p, Abc_Obj_t * pRoot, Cut_Cut_t * pCut, Vec_Ptr_t * vFaninsCur, int nNodesSaved, int LevelMax, int * pGainBest, int fPlaceEnable );
static Dec_Graph_t * Rwr_CutEvaluateP( Rwr_Man_t * p, Abc_Obj_t * pRoot, Cut_Cut_t * pCut, Vec_Ptr_t * vFaninsCur, int nNodesSaved, float HPWLSaved, float overflowBefore, float overflowAfter, int LevelMax, float * pGainBest, int fPlaceEnable, int fSimple, int fClustering );
static int Rwr_CutIsBoolean( Abc_Obj_t * pObj, Vec_Ptr_t * vLeaves );
static int Rwr_CutCountNumNodes( Abc_Obj_t * pObj, Cut_Cut_t * pCut );
static int Rwr_NodeGetDepth_rec( Abc_Obj_t * pObj, Vec_Ptr_t * vLeaves );

int global_counter = 0;

static inline float Rwr_ObjWireDelay( Abc_Obj_t * pFrom, Abc_Obj_t * pTo )
{
    if ( Abc_ObjIsTerm(pFrom) || Abc_ObjIsTerm(pTo) )
        return 0.0f;
    return sqrtf( (pFrom->xPos - pTo->xPos) * (pFrom->xPos - pTo->xPos) +
                  (pFrom->yPos - pTo->yPos) * (pFrom->yPos - pTo->yPos) );
}

static void Rwr_CutCollectNodes_rec( Abc_Obj_t * pObj, Vec_Ptr_t * vNodes, Vec_Ptr_t * vSeen )
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
            Rwr_CutCollectNodes_rec( pFanin, vNodes, vSeen );
    Vec_PtrPush( vNodes, pObj );
}

static void Rwr_CutUpdateArrival( Abc_Obj_t * pObj )
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
        float ArrivalG = pFanin->arrivalG + Rwr_ObjWireDelay( pFanin, pObj );
        if ( pObj->arrivalT < ArrivalT )
            pObj->arrivalT = ArrivalT;
        if ( pObj->arrivalG < ArrivalG )
            pObj->arrivalG = ArrivalG;
    }
}

static void Rwr_CutUpdateRequired( Abc_Obj_t * pObj )
{
    Abc_Obj_t * pFanout;
    int i, nFanouts;

    pObj->reqT = ABC_INFINITY;
    pObj->reqG = (float)ABC_INFINITY;
    nFanouts = Vec_IntSize( &pObj->vFanouts ) - (pObj->marked ? 1 : 0);
    for ( i = 0; i < nFanouts; ++i )
    {
        pFanout = Abc_NtkObj( pObj->pNtk, Vec_IntEntry(&pObj->vFanouts, i) );
        int ReqT = pFanout->reqT;
        float ReqG = pFanout->reqG;
        if ( !Abc_ObjIsCo(pFanout) )
        {
            ReqT -= 1;
            ReqG -= Rwr_ObjWireDelay( pObj, pFanout );
        }
        if ( pObj->reqT > ReqT )
            pObj->reqT = ReqT;
        if ( pObj->reqG > ReqG )
            pObj->reqG = ReqG;
    }
}

static void Rwr_CutUpdateTiming( Abc_Obj_t * pRoot, Vec_Ptr_t * vNodes, Vec_Ptr_t * vSeen )
{
    Abc_Obj_t * pObj;
    int i;

    Vec_PtrClear( vNodes );
    Vec_PtrClear( vSeen );
    Rwr_CutCollectNodes_rec( pRoot, vNodes, vSeen );
    Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pObj, i )
        Rwr_CutUpdateArrival( pObj );
    Vec_PtrForEachEntryReverse( Abc_Obj_t *, vNodes, pObj, i )
        Rwr_CutUpdateRequired( pObj );
}


////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Performs rewriting for one node.]

  Description [This procedure considers all the cuts computed for the node
  and tries to rewrite each of them using the "forest" of different AIG
  structures precomputed and stored in the RWR manager. 
  Determines the best rewriting and computes the gain in the number of AIG
  nodes in the final network. In the end, p->vFanins contains information 
  about the best cut that can be used for rewriting, while p->pGraph gives 
  the decomposition dag (represented using decomposition graph data structure).
  Returns gain in the number of nodes or -1 if node cannot be rewritten.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Rwr_NodeRewrite( Rwr_Man_t * p, Cut_Man_t * pManCut, Abc_Obj_t * pNode, int fUpdateLevel, int fUseZeros, int fPlaceEnable )
{
    int fVeryVerbose = 0;
    Dec_Graph_t * pGraph;
    Cut_Cut_t * pCut;//, * pTemp;
    Abc_Obj_t * pFanin;
    unsigned uPhase;
    unsigned uTruthBest = 0; // Suppress "might be used uninitialized"
    unsigned uTruth;
    char * pPerm;
    int Required, nNodesSaved;
    int nNodesSaveCur = -1; // Suppress "might be used uninitialized"
    int i, GainCur = -1, GainBest = -1;
    abctime clk, clk2;//, Counter;

    p->nNodesConsidered++;
    // get the required times
    Required = fUpdateLevel? Abc_ObjRequiredLevel(pNode) : ABC_INFINITY;

    // get the node's cuts
clk = Abc_Clock();
    pCut = (Cut_Cut_t *)Abc_NodeGetCutsRecursive( pManCut, pNode, 0, 0 );
    assert( pCut != NULL );
p->timeCut += Abc_Clock() - clk;

//printf( " %d", Rwr_CutCountNumNodes(pNode, pCut) );
/*
    Counter = 0;
    for ( pTemp = pCut->pNext; pTemp; pTemp = pTemp->pNext )
        Counter++;
    printf( "%d ", Counter );
*/
    // go through the cuts
clk = Abc_Clock();
    for ( pCut = pCut->pNext; pCut; pCut = pCut->pNext )
    {
        // consider only 4-input cuts
        if ( pCut->nLeaves < 4 )
            continue;
//            Cut_CutPrint( pCut, 0 ), printf( "\n" );

        // get the fanin permutation
        uTruth = 0xFFFF & *Cut_CutReadTruth(pCut);
        pPerm = p->pPerms4[ (int)p->pPerms[uTruth] ];
        uPhase = p->pPhases[uTruth];
        // collect fanins with the corresponding permutation/phase
        Vec_PtrClear( p->vFaninsCur );
        Vec_PtrFill( p->vFaninsCur, (int)pCut->nLeaves, 0 );
        for ( i = 0; i < (int)pCut->nLeaves; i++ )
        {
            pFanin = Abc_NtkObj( pNode->pNtk, pCut->pLeaves[(int)pPerm[i]] );
            if ( pFanin == NULL )
                break;
            pFanin = Abc_ObjNotCond(pFanin, ((uPhase & (1<<i)) > 0) );
            Vec_PtrWriteEntry( p->vFaninsCur, i, pFanin );
        }
        if ( i != (int)pCut->nLeaves )
        {
            p->nCutsBad++;
            continue;
        }
        p->nCutsGood++;

        {
            int Counter = 0;
            Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, i )
                if ( Abc_ObjFanoutNum(Abc_ObjRegular(pFanin)) == 1 )
                    Counter++;
            if ( Counter > 2 )
                continue;
        }

clk2 = Abc_Clock();
/*
        printf( "Considering: (" );
        Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, i )
            printf( "%d ", Abc_ObjFanoutNum(Abc_ObjRegular(pFanin)) );
        printf( ")\n" );
*/
        // mark the fanin boundary 
        Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, i )
            Abc_ObjRegular(pFanin)->vFanouts.nSize++;

        // label MFFC with current ID
        Abc_NtkIncrementTravId( pNode->pNtk );
        nNodesSaved = Abc_NodeMffcLabelAig( pNode );
        // unmark the fanin boundary
        Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, i )
            Abc_ObjRegular(pFanin)->vFanouts.nSize--;
p->timeMffc += Abc_Clock() - clk2;

        // evaluate the cut
clk2 = Abc_Clock();
        pGraph = Rwr_CutEvaluate( p, pNode, pCut, p->vFaninsCur, nNodesSaved, Required, &GainCur, fPlaceEnable );
p->timeEval += Abc_Clock() - clk2;

        // check if the cut is better than the current best one
        if ( pGraph != NULL && GainBest < GainCur )
        {
            // save this form
            nNodesSaveCur = nNodesSaved;
            GainBest  = GainCur;
            p->pGraph  = pGraph;
            p->fCompl = ((uPhase & (1<<4)) > 0);
            uTruthBest = 0xFFFF & *Cut_CutReadTruth(pCut);
            // collect fanins in the
            Vec_PtrClear( p->vFanins );
            Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, i )
                Vec_PtrPush( p->vFanins, pFanin );
        }
    }
p->timeRes += Abc_Clock() - clk;

    if ( GainBest == -1 )
        return -1;
/*
    if ( GainBest > 0 )
    {
        printf( "Class %d  ", p->pMap[uTruthBest] );
        printf( "Gain = %d. Node %d : ", GainBest, pNode->Id );
        Vec_PtrForEachEntry( Abc_Obj_t *, p->vFanins, pFanin, i )
            printf( "%d ", Abc_ObjRegular(pFanin)->Id );
        Dec_GraphPrint( stdout, p->pGraph, NULL, NULL );
        printf( "\n" );
    }
*/

//    printf( "%d", nNodesSaveCur - GainBest );
/*
    if ( GainBest > 0 )
    {
        if ( Rwr_CutIsBoolean( pNode, p->vFanins ) )
            printf( "b" );
        else
        {
            printf( "Node %d : ", pNode->Id );
            Vec_PtrForEachEntry( Abc_Obj_t *, p->vFanins, pFanin, i )
                printf( "%d ", Abc_ObjRegular(pFanin)->Id );
            printf( "a" );
        }
    }
*/
/*
    if ( GainBest > 0 )
        if ( p->fCompl )
            printf( "c" );
        else
            printf( "." );
*/

    // copy the leaves
    Vec_PtrForEachEntry( Abc_Obj_t *, p->vFanins, pFanin, i )
        Dec_GraphNode((Dec_Graph_t *)p->pGraph, i)->pFunc = pFanin;
/*
    printf( "(" );
    Vec_PtrForEachEntry( Abc_Obj_t *, p->vFanins, pFanin, i )
        printf( " %d", Abc_ObjRegular(pFanin)->vFanouts.nSize - 1 );
    printf( " )  " );
*/
//    printf( "%d ", Rwr_NodeGetDepth_rec( pNode, p->vFanins ) );

    p->nScores[p->pMap[uTruthBest]]++;
    p->nNodesGained += GainBest;
    if ( fUseZeros || GainBest > 0 )
    {
        p->nNodesRewritten++;
    }

    // report the progress
    if ( fVeryVerbose && GainBest > 0 )
    {
        printf( "Node %6s :   ", Abc_ObjName(pNode) );
        printf( "Fanins = %d. ", p->vFanins->nSize );
        printf( "Save = %d.  ", nNodesSaveCur );
        printf( "Add = %d.  ",  nNodesSaveCur-GainBest );
        printf( "GAIN = %d.  ", GainBest );
        printf( "Cone = %d.  ", p->pGraph? Dec_GraphNodeNum((Dec_Graph_t *)p->pGraph) : 0 );
        printf( "Class = %d.  ", p->pMap[uTruthBest] );
        printf( "\n" );
    }
    return GainBest;
}

float Rwr_NodeRewriteP( Rwr_Man_t * p, Cut_Man_t * pManCut, Abc_Obj_t * pNode, int fUpdateLevel, int fUseZeros, int fPlaceEnable, int fSimple, int fClustering )
{
    int fVeryVerbose = 0;
    Dec_Graph_t * pGraph;
    Cut_Cut_t * pCut;//, * pTemp;
    Abc_Obj_t * pFanin;
    unsigned uPhase;
    unsigned uTruthBest = 0; // Suppress "might be used uninitialized"
    unsigned uTruth;
    char * pPerm;
    int Required, nNodesSaved;
    float HPWLSaved;
    int nNodesSaveCur = -1; // Suppress "might be used uninitialized"
    float nHPWLSaveCur = -1;
    int i;
    float GainCur = -1, GainBest = -1;
    abctime clk, clk2;//, Counter;

    Abc_Ntk_t * pNtk = pNode->pNtk;
    float overflowSaved;
    Vec_Ptr_t * vCutNodes = Vec_PtrAlloc( 32 );
    Vec_Ptr_t * vCutSeen = Vec_PtrAlloc( 32 );

    p->nNodesConsidered++;
    // get the required times
    Required = fUpdateLevel? Abc_ObjRequiredLevel(pNode) : ABC_INFINITY;

    // get the node's cuts
clk = Abc_Clock();
    pCut = (Cut_Cut_t *)Abc_NodeGetCutsRecursive( pManCut, pNode, 0, 0 );
    assert( pCut != NULL );
p->timeCut += Abc_Clock() - clk;

//printf( " %d", Rwr_CutCountNumNodes(pNode, pCut) );
/*
    Counter = 0;
    for ( pTemp = pCut->pNext; pTemp; pTemp = pTemp->pNext )
        Counter++;
    printf( "%d ", Counter );
*/
    // go through the cuts
clk = Abc_Clock();


    // // global overflow computation
    float overflow1 = 0;
    // for (int i = 0; i < pNtk->nBins; ++i)
    // {
    //     // printf("%.2f && ", pNtk->binsDensity[i]);
    //     overflow1 += (pNtk->binsDensity[i] > 1) ? pNtk->binsDensity[i] - 1 : 0;
    // }
    // overflow1 *= pNtk->binModuleRatio;

    // printf("Overflow1 = %f\n", overflow1);


    for ( pCut = pCut->pNext; pCut; pCut = pCut->pNext )
    {
        // consider only 4-input cuts
        if ( pCut->nLeaves < 4 )
            continue;
//            Cut_CutPrint( pCut, 0 ), printf( "\n" );

        // get the fanin permutation
        uTruth = 0xFFFF & *Cut_CutReadTruth(pCut);
        pPerm = p->pPerms4[ (int)p->pPerms[uTruth] ];
        uPhase = p->pPhases[uTruth];
        // collect fanins with the corresponding permutation/phase
        Vec_PtrClear( p->vFaninsCur );
        Vec_PtrFill( p->vFaninsCur, (int)pCut->nLeaves, 0 );
        for ( i = 0; i < (int)pCut->nLeaves; i++ )
        {
            pFanin = Abc_NtkObj( pNode->pNtk, pCut->pLeaves[(int)pPerm[i]] );
            if ( pFanin == NULL )
                break;
            pFanin = Abc_ObjNotCond(pFanin, ((uPhase & (1<<i)) > 0) );
            Vec_PtrWriteEntry( p->vFaninsCur, i, pFanin );
        }
        if ( i != (int)pCut->nLeaves )
        {
            p->nCutsBad++;
            continue;
        }
        p->nCutsGood++;

        {
            int Counter = 0;
            Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, i )
                if ( Abc_ObjFanoutNum(Abc_ObjRegular(pFanin)) == 1 )
                    Counter++;
            if ( Counter > 2 )
                continue;
        }

clk2 = Abc_Clock();
/*
        printf( "Considering: (" );
        Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, i )
            printf( "%d ", Abc_ObjFanoutNum(Abc_ObjRegular(pFanin)) );
        printf( ")\n" );
*/
        // mark the fanin boundary 
        Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, i )
            // Abc_ObjRegular(pFanin)->vFanouts.nSize++;
        {
            Abc_ObjRegular(pFanin)->vFanouts.nSize++;
            Abc_ObjRegular(pFanin)->marked = 1;
            // printf("%d, ", Abc_ObjRegular(pFanin)->Id);
        }

        // printf("\nOverflow0 = %f\n", overflow);
        // printf("binOriX = %f, binOriY = %f, binStepX = %f, binStepY = %f\n", pNtk->binOriX, pNtk->binOriY, pNtk->binStepX, pNtk->binStepY);
        // label MFFC with current ID
        Abc_NtkIncrementTravId( pNode->pNtk );
        pNode->pNtk->nPosTmp = 0;
        HPWLSaved = Abc_NodeMffcLabelAigHPWL( pNode, &overflowSaved, fClustering );
        nNodesSaved = Abc_NodeMffcLabelAig( pNode );
        Rwr_CutUpdateTiming( pNode, vCutNodes, vCutSeen );
        // printf("NodeID = %d, nNodeSaved = %d, HPWLSaved = %.2f\n", pNode->Id, nNodesSaved, HPWLSaved);
        // unmark the fanin boundary
        Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, i )
            // Abc_ObjRegular(pFanin)->vFanouts.nSize--;
        {
            Abc_ObjRegular(pFanin)->vFanouts.nSize--;
            Abc_ObjRegular(pFanin)->marked = 0;
            // printf("%d, %d, %f, %f **", i, Abc_ObjRegular(pFanin)->Id, Abc_ObjRegular(pFanin)->xPos, Abc_ObjRegular(pFanin)->yPos);
        }
        // printf("Root: %d, %f, %f\n", pNode->Id, pNode->xPos, pNode->yPos);
p->timeMffc += Abc_Clock() - clk2;

        // // global overflow test
        // float overflow2 = 0;
        // for (int i = 0; i < pNtk->nBins; ++i)
        // {
        //     // printf("%.2f && ", pNtk->binsDensity[i]);
        //     overflow2 += (pNtk->binsDensity[i] > 1) ? pNtk->binsDensity[i] - 1 : 0;
        // }
        // overflow2 *= pNtk->binModuleRatio;

        // printf("\n%f\n", overflow2);

        // evaluate the cut
clk2 = Abc_Clock();
        pGraph = Rwr_CutEvaluateP( p, pNode, pCut, p->vFaninsCur, nNodesSaved, HPWLSaved, overflow1, overflow1 + overflowSaved * pNtk->binModuleRatio, Required, &GainCur, fPlaceEnable, fSimple, fClustering );
p->timeEval += Abc_Clock() - clk2;

        // check if the cut is better than the current best one
        if ( pGraph != NULL && GainBest < GainCur )
        {
            // save this form
            nHPWLSaveCur = HPWLSaved;
            GainBest  = GainCur;
            p->pGraph  = pGraph;
            p->fCompl = ((uPhase & (1<<4)) > 0);
            uTruthBest = 0xFFFF & *Cut_CutReadTruth(pCut);
            // collect fanins in the
            Vec_PtrClear( p->vFanins );
            Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, i )
                Vec_PtrPush( p->vFanins, pFanin );
        }

        // binDensity recovery
        float xMin, yMin, xMax, yMax;
        int xb0, xb1, yb0, yb1;
        float x0, x1, y0, y1;
        int nShareCap = Abc_MaxInt( pNtk->binDimX, pNtk->binDimY );
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
        
        // // global overflow test
        // float overflow = 0;
        // for (int i = 0; i < pNtk->nBins; ++i)
        // {
        //     // printf("%.2f && ", pNtk->binsDensity[i]);
        //     overflow += (pNtk->binsDensity[i] > 1) ? pNtk->binsDensity[i] - 1 : 0;
        // }
        // overflow *= pNtk->binModuleRatio;
        // printf("\nOverflow2 = %f\n", overflow);
    }
p->timeRes += Abc_Clock() - clk;

    // if ( GainBest == -1 )
    if ( GainBest <= 0 )
    {
        Vec_PtrFree( vCutSeen );
        Vec_PtrFree( vCutNodes );
        return -1;
    }
/*
    if ( GainBest > 0 )
    {
        printf( "Class %d  ", p->pMap[uTruthBest] );
        printf( "Gain = %d. Node %d : ", GainBest, pNode->Id );
        Vec_PtrForEachEntry( Abc_Obj_t *, p->vFanins, pFanin, i )
            printf( "%d ", Abc_ObjRegular(pFanin)->Id );
        Dec_GraphPrint( stdout, p->pGraph, NULL, NULL );
        printf( "\n" );
    }
*/

//    printf( "%d", nNodesSaveCur - GainBest );
/*
    if ( GainBest > 0 )
    {
        if ( Rwr_CutIsBoolean( pNode, p->vFanins ) )
            printf( "b" );
        else
        {
            printf( "Node %d : ", pNode->Id );
            Vec_PtrForEachEntry( Abc_Obj_t *, p->vFanins, pFanin, i )
                printf( "%d ", Abc_ObjRegular(pFanin)->Id );
            printf( "a" );
        }
    }
*/
/*
    if ( GainBest > 0 )
        if ( p->fCompl )
            printf( "c" );
        else
            printf( "." );
*/
    // float overflowF = 0;
    // for (int i = 0; i < pNtk->nBins; ++i)
    // {
    //     // printf("%.2f && ", pNtk->binsDensity[i]);
    //     overflowF += (pNtk->binsDensity[i] > 1) ? pNtk->binsDensity[i] - 1 : 0;
    // }
    // overflowF *= pNtk->binModuleRatio;
    // printf("\nOverflowF0 = %f\n", overflowF);
    
    // copy the leaves
    Vec_PtrForEachEntry( Abc_Obj_t *, p->vFanins, pFanin, i )
        Dec_GraphNode((Dec_Graph_t *)p->pGraph, i)->pFunc = pFanin;

    // label MFFC with current ID
    Abc_NtkIncrementTravId( pNode->pNtk );

    // // check the global HPWL before destruction
    // float totalHPWL = 0;
    // Abc_Obj_t * pNodeAig, * pFanout;
    // float xMin, xMax, yMin, yMax;
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
    //     if ( xMin == 1000000 )
    //         continue;
    //     totalHPWL += xMax - xMin + yMax - yMin;
    // }
    // printf("totalHPWLbeforeDestruction = %f\n", totalHPWL);

    // mark the fanin boundary 
    Vec_PtrForEachEntry( Abc_Obj_t *, p->vFanins, pFanin, i )
        // Abc_ObjRegular(pFanin)->vFanouts.nSize++;
    {
        Abc_ObjRegular(pFanin)->vFanouts.nSize++;
        Abc_ObjRegular(pFanin)->marked = 1;
        // printf("%d, ", Abc_ObjRegular(pFanin)->Id);
    }

    pNode->pNtk->nPosTmp = 0;
    HPWLSaved = Abc_NodeMffcLabelAigHPWL( pNode, &overflowSaved, fClustering );
    // unmark the fanin boundary
    Vec_PtrForEachEntry( Abc_Obj_t *, p->vFanins, pFanin, i )
        // Abc_ObjRegular(pFanin)->vFanouts.nSize--;
    {
        Abc_ObjRegular(pFanin)->vFanouts.nSize--;
        Abc_ObjRegular(pFanin)->marked = 0;
    }
    // printf("!!Final: HPWLSaved = %.2f\n", HPWLSaved);

    // // check the global HPWL after destruction
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
    //     if ( xMin == 1000000 )
    //         continue;
    //     totalHPWL += xMax - xMin + yMax - yMin;
    // }
    // printf("totalHPWLafterDestruction = %f\n", totalHPWL);
    // // global overflow test
    // float overflowF1 = 0;
    // for (int i = 0; i < pNtk->nBins; ++i)
    // {
    //     // printf("%.2f && ", pNtk->binsDensity[i]);
    //     overflowF1 += (pNtk->binsDensity[i] > 1) ? pNtk->binsDensity[i] - 1 : 0;
    // }
    // overflowF1 *= pNtk->binModuleRatio;
    // printf("\nOverflowF1 = %f\n", overflowF1);
/*
    printf( "(" );
    Vec_PtrForEachEntry( Abc_Obj_t *, p->vFanins, pFanin, i )
        printf( " %d", Abc_ObjRegular(pFanin)->vFanouts.nSize - 1 );
    printf( " )  " );
*/
//    printf( "%d ", Rwr_NodeGetDepth_rec( pNode, p->vFanins ) );

    p->nScores[p->pMap[uTruthBest]]++;
    p->nNodesGained += GainBest;
    if ( fUseZeros || GainBest > 0 )
    {
        p->nNodesRewritten++;
    }

    // report the progress
    if ( fVeryVerbose && GainBest > 0 )
    {
        printf( "Node %6s :   ", Abc_ObjName(pNode) );
        printf( "Fanins = %d. ", p->vFanins->nSize );
        printf( "Save = %d.  ", nNodesSaveCur );
        printf( "Add = %d.  ",  nNodesSaveCur-GainBest );
        printf( "GAIN = %d.  ", GainBest );
        printf( "Cone = %d.  ", p->pGraph? Dec_GraphNodeNum((Dec_Graph_t *)p->pGraph) : 0 );
        printf( "Class = %d.  ", p->pMap[uTruthBest] );
        printf( "\n" );
    }
    Vec_PtrFree( vCutSeen );
    Vec_PtrFree( vCutNodes );
    return GainBest;
}


// --------------------------------------------------------------------------
// HELPER: Trivial SOP (always outputs 1)
// --------------------------------------------------------------------------
char * Abc_SopCreateTrivial( Mem_Flex_t * pMan, int nFanins )
{
    char * pSop;
    int i;
    
    // We need space for: nFanins (dashes) + " 1\n" + null terminator
    // Total = nFanins + 4 chars
    pSop = (char *)Mem_FlexEntryFetch( pMan, nFanins + 4 );
    
    // Fill with dashes (Don't Cares)
    for ( i = 0; i < nFanins; i++ )
        pSop[i] = '-';
        
    // Append the suffix " 1\n"
    pSop[nFanins]   = ' ';
    pSop[nFanins+1] = '1';
    pSop[nFanins+2] = '\n';
    pSop[nFanins+3] = '\0';
    
    return pSop;
}

// --------------------------------------------------------------------------
// HELPER: Recursive Driver Collector (Unchanged)
// --------------------------------------------------------------------------
void Abc_NtkRoughMapCollect_rec(Abc_Obj_t * pObj, Vec_Ptr_t * vDrivers)
{
    // If this node has a copy (meaning it is a Keeper), stop and collect it.
    if (pObj->pCopy)
    {
        Vec_PtrPushUnique(vDrivers, pObj->pCopy);
        return;
    }

    // If no copy, recurse on fanins
    Abc_Obj_t * pFanin;
    int i;
    Abc_ObjForEachFanin(pObj, pFanin, i)
    {
        Abc_NtkRoughMapCollect_rec(pFanin, vDrivers);
    }
}

// --------------------------------------------------------------------------
// MAIN FUNCTION: AIG to Rough Logic Network (Corrected)
// --------------------------------------------------------------------------
Abc_Ntk_t * Abc_NtkToRoughLogic(Abc_Ntk_t * pNtk)
{
    Abc_Ntk_t * pNtkNew;
    Abc_Obj_t * pObj, * pObjNew, * pDriver, * pFanout;
    Vec_Ptr_t * vDrivers;
    int i, k, m;
    int bIsKeeper;

    assert(Abc_NtkIsStrash(pNtk));

    pNtkNew = Abc_NtkAlloc(ABC_NTK_LOGIC, ABC_FUNC_SOP, 1);
    pNtkNew->pName = Extra_UtilStrsav(pNtk->pName);

    // 1. Map Constant 1 (Always a Keeper)
    pObj = Abc_AigConst1(pNtk);
    pObj->pCopy = Abc_NtkCreateNodeConst1(pNtkNew);

    // 2. Map Primary Inputs (Always Keepers)
    Abc_NtkForEachPi(pNtk, pObj, i)
    {
        pObjNew = Abc_NtkDupObj(pNtkNew, pObj, 1);
        pObj->pCopy = pObjNew;
    }

    // 3. Map Internal Nodes
    vDrivers = Vec_PtrAlloc(100);
    Abc_NtkForEachNode(pNtk, pObj, i)
    {
        // --- DETERMINE IF NODE IS A KEEPER ---
        // Condition 1: Fanout > 1
        bIsKeeper = (Abc_ObjFanoutNum(pObj) > 1);

        // Condition 2: Drives a PO?
        // (Even if Fanout is 1, if that single fanout is a PO, we MUST keep this node)
        if (!bIsKeeper) 
        {
            Abc_ObjForEachFanout(pObj, pFanout, m)
            {
                if (Abc_ObjIsPo(pFanout))
                {
                    bIsKeeper = 1;
                    break;
                }
            }
        }

        if (bIsKeeper)
        {
            // Create the new logic node
            pObjNew = Abc_NtkCreateNode(pNtkNew);
            pObj->pCopy = pObjNew;

            // Collect inputs from upstream Keepers
            Vec_PtrClear(vDrivers);
            Abc_Obj_t * pFanin;
            int n;
            Abc_ObjForEachFanin(pObj, pFanin, n)
            {
                Abc_NtkRoughMapCollect_rec(pFanin, vDrivers);
            }

            // Connect inputs
            Vec_PtrForEachEntry(Abc_Obj_t *, vDrivers, pDriver, k)
            {
                Abc_ObjAddFanin(pObjNew, pDriver);
            }

            // Set Trivial Function (Constant 1)
            pObjNew->pData = Abc_SopCreateTrivial((Mem_Flex_t *)pNtkNew->pManFunc, Abc_ObjFaninNum(pObjNew));
        }
        else
        {
            // Absorb this node (it acts as a transparent wire)
            pObj->pCopy = NULL;
        }
    }
    Vec_PtrFree(vDrivers);

    // 4. Map Primary Outputs
    Abc_NtkForEachPo(pNtk, pObj, i)
    {
        pObjNew = Abc_NtkDupObj(pNtkNew, pObj, 1);

        // Get the driver in the AIG
        Abc_Obj_t * pAigDriver = Abc_ObjFanin0(pObj);

        // Because we forced all PO drivers to be Keepers in step 3 (or PIs/Const in 1/2),
        // pAigDriver->pCopy is GUARANTEED to be non-NULL.
        assert(pAigDriver->pCopy != NULL); 

        Abc_ObjAddFanin(pObjNew, pAigDriver->pCopy);
    }

    // 5. Final Check
    if (!Abc_NtkCheck(pNtkNew))
    {
        printf("Abc_NtkToRoughLogic: The network check has failed.\n");
        Abc_NtkDelete(pNtk);
        return NULL;
    }

    Abc_NtkDelete(pNtk);
    return pNtkNew;
}


float Rwr_NodeRewriteExh( Rwr_Man_t * p, Cut_Man_t * pManCut, Abc_Obj_t * pNode, int fUpdateLevel, int fUseZeros, int fPlaceEnable, Abc_Frame_t * pAbcLocal )
{
    extern int Dec_GraphToNetworkCount( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int NodeMax, int LevelMax );
    extern Abc_Obj_t * Dec_GraphToNetwork( Abc_Ntk_t * pNtk, Dec_Graph_t * pGraph );
    extern int Abc_AigReplace( Abc_Aig_t * pMan, Abc_Obj_t * pOld, Abc_Obj_t * pNew, int fUpdateLevel );

    int fVeryVerbose = 0;
    Dec_Graph_t * pGraph;
    Cut_Cut_t * pCut;//, * pTemp;
    Abc_Obj_t * pFanin;
    unsigned uPhase;
    unsigned uTruthBest = 0; // Suppress "might be used uninitialized"
    unsigned uTruth;
    char * pPerm;
    int Required, nNodesSaved;
    float HPWLSaved;
    int nNodesSaveCur = -1; // Suppress "might be used uninitialized"
    float nHPWLSaveCur = -1;
    int i;
    float GainCur = -1, GainBest = -1;
    abctime clk, clk2;//, Counter;

    Abc_Ntk_t * pNtk = pNode->pNtk;
    float overflowSaved;

    Abc_Frame_t * pAbc = Abc_FrameGetGlobalFrame();
    
    Abc_Ntk_t * pNtkDup;
    Abc_Obj_t * pNodeDup;
    Abc_Obj_t * pRootNew;
    int j;
    int RetValue;
    float hpwlBefore, hpwlAfter;

    p->nNodesConsidered++;
    // get the required times
    Required = fUpdateLevel? Abc_ObjRequiredLevel(pNode) : ABC_INFINITY;

    // get the node's cuts
clk = Abc_Clock();
    pCut = (Cut_Cut_t *)Abc_NodeGetCutsRecursive( pManCut, pNode, 0, 0 );
    assert( pCut != NULL );
p->timeCut += Abc_Clock() - clk;

//printf( " %d", Rwr_CutCountNumNodes(pNode, pCut) );
/*
    Counter = 0;
    for ( pTemp = pCut->pNext; pTemp; pTemp = pTemp->pNext )
        Counter++;
    printf( "%d ", Counter );
*/
    // go through the cuts
clk = Abc_Clock();

    for ( pCut = pCut->pNext; pCut; pCut = pCut->pNext )
    {
        // consider only 4-input cuts
        if ( pCut->nLeaves < 4 )
            continue;
//            Cut_CutPrint( pCut, 0 ), printf( "\n" );

        // get the fanin permutation
        uTruth = 0xFFFF & *Cut_CutReadTruth(pCut);
        pPerm = p->pPerms4[ (int)p->pPerms[uTruth] ];
        uPhase = p->pPhases[uTruth];
        // collect fanins with the corresponding permutation/phase
        Vec_PtrClear( p->vFaninsCur );
        Vec_PtrFill( p->vFaninsCur, (int)pCut->nLeaves, 0 );
        for ( i = 0; i < (int)pCut->nLeaves; i++ )
        {
            pFanin = Abc_NtkObj( pNode->pNtk, pCut->pLeaves[(int)pPerm[i]] );
            if ( pFanin == NULL )
                break;
            pFanin = Abc_ObjNotCond(pFanin, ((uPhase & (1<<i)) > 0) );
            Vec_PtrWriteEntry( p->vFaninsCur, i, pFanin );
        }
        if ( i != (int)pCut->nLeaves )
        {
            p->nCutsBad++;
            continue;
        }
        p->nCutsGood++;

        {
            int Counter = 0;
            Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, i )
                if ( Abc_ObjFanoutNum(Abc_ObjRegular(pFanin)) == 1 )
                    Counter++;
            if ( Counter > 2 )
                continue;
        }

clk2 = Abc_Clock();


        // evaluate the cut
clk2 = Abc_Clock();
        Vec_Ptr_t * vSubgraphs;
        Dec_Graph_t * pGraphBest = NULL; // Suppress "might be used uninitialized"
        Dec_Graph_t * pGraphCur;
        Rwr_Node_t * pNodeR, * pFaninR;
        int nNodesAdded, k;
        unsigned uTruth;
        float CostBest;//, CostCur;

        // find the matching class of subgraphs
        uTruth = 0xFFFF & *Cut_CutReadTruth(pCut);
        vSubgraphs = Vec_VecEntry( p->vClasses, p->pMap[uTruth] );
        p->nSubgraphs += vSubgraphs->nSize;
        // determine the best subgraph
        CostBest = ABC_INFINITY;
        Vec_PtrForEachEntry( Rwr_Node_t *, vSubgraphs, pNodeR, i )
        {
            // get the current graph
            pGraphCur = (Dec_Graph_t *)pNodeR->pNext;
            // copy the leaves
            Vec_PtrForEachEntry( Rwr_Node_t *, p->vFaninsCur, pFaninR, k )
                Dec_GraphNode(pGraphCur, k)->pFunc = pFaninR;
            // detect how many unlabeled nodes will be reused
            nNodesAdded = Dec_GraphToNetworkCount( pNode, pGraphCur, 1000000, Required );
            
            if ( nNodesAdded == -1 )
                continue;

            // get the HPWL of the current Ntk
            pNtkDup = Abc_NtkDup(pNtk);
            Abc_NtkReassignIds( pNtkDup );
            // rough technology mapping grouping MFFC nodes
            // pNtkDup = Abc_NtkToRoughLogic( pNtkDup );
            Abc_FrameReplaceCurrentNetwork( pAbcLocal, pNtkDup );
            // Cmd_CommandExecute( pAbcLocal, "ps" );
            Cmd_CommandExecute( pAbcLocal, "&get" );
            Cmd_CommandExecute( pAbcLocal, "&nf" );
            Cmd_CommandExecute( pAbcLocal, "&put" );
            // Cmd_CommandExecute( pAbcLocal, "ps" );
            // Cmd_CommandExecute( pAbcLocal, "amap" );
            // Cmd_CommandExecute( pAbcLocal, "ps" );
            Cmd_CommandExecute( pAbcLocal, "replace" );
            pNtkDup = Abc_FrameReadNtk(pAbcLocal);
            hpwlBefore = pNtkDup->globalHpwl;

            // // use GNN model to predict the HPWL-related score of the current network
            // float hpwlScoreBefore, hpwlScoreAfter;
            // pNtkDup = Abc_NtkDup(pNtk); // Clone network to preserve state
            // Abc_NtkReassignIds( pNtkDup );
            // hpwlScoreBefore = Abc_CallGNN(pNtkDup);
            // Abc_NtkDelete(pNtkDup); // Clean up clone

            // rewrite the node and get the HPWL of the new Ntk
            pNtkDup = Abc_NtkDup(pNtk);
            // change p->vFaninsCur to be from the duplicated Ntk
            Vec_PtrClear( p->vFaninsCur );
            Vec_PtrFill( p->vFaninsCur, (int)pCut->nLeaves, 0 );
            for ( k = 0; k < (int)pCut->nLeaves; k++ )
            {
                pFanin = Abc_NtkObj(pNtk, pCut->pLeaves[(int)pPerm[k]])->pCopy;
                if ( pFanin == NULL )
                    break;
                pFanin = Abc_ObjNotCond(pFanin, ((uPhase & (1<<k)) > 0) );
                Vec_PtrWriteEntry( p->vFaninsCur, k, pFanin );
            }
            // copy the leaves
            Vec_PtrForEachEntry( Rwr_Node_t *, p->vFaninsCur, pFaninR, k )
                Dec_GraphNode(pGraphCur, k)->pFunc = pFaninR;

            // Abc_NtkForEachObj(pNtkDup, pNodeDup, j)
            //     if ( pNodeDup->Id == pNode->Id )
            //         break;
            // assert (pNodeDup->Id == pNode->Id);
            pNodeDup = pNode->pCopy;
            // complement the FF if needed
            if ( ((uPhase & (1<<4)) > 0) ) Dec_GraphComplement( pGraphCur );
            pRootNew = Dec_GraphToNetwork( pNtkDup, pGraphCur );
            RetValue = Abc_AigReplace( (Abc_Aig_t *)pNtkDup->pManFunc, pNodeDup, pRootNew, fUpdateLevel );
            if (RetValue == -1)
            {
                // recover the FF
                if ( ((uPhase & (1<<4)) > 0) ) Dec_GraphComplement( pGraphCur );
                // restore p->vFaninsCur to be from the original Ntk
                Vec_PtrClear( p->vFaninsCur );
                Vec_PtrFill( p->vFaninsCur, (int)pCut->nLeaves, 0 );
                for ( k = 0; k < (int)pCut->nLeaves; k++ )
                {
                    pFanin = Abc_NtkObj( pNtk, pCut->pLeaves[(int)pPerm[k]] );
                    if ( pFanin == NULL )
                        break;
                    pFanin = Abc_ObjNotCond(pFanin, ((uPhase & (1<<k)) > 0) );
                    Vec_PtrWriteEntry( p->vFaninsCur, k, pFanin );
                }
                Abc_NtkDelete(pNtkDup);
                continue;
            }
            else
            {
                Abc_Ntk_t * pNtkTmp = Abc_NtkDup(pNtkDup);
                Abc_NtkReassignIds( pNtkTmp );

                // get the HPWL of the updated Ntk
                Abc_NtkReassignIds( pNtkDup );
                // rough technology mapping grouping MFFC nodes
                // pNtkDup = Abc_NtkToRoughLogic( pNtkDup );
                Abc_FrameReplaceCurrentNetwork( pAbcLocal, pNtkDup );
                // Cmd_CommandExecute( pAbcLocal, "ps" );
                Cmd_CommandExecute( pAbcLocal, "&get" );
                Cmd_CommandExecute( pAbcLocal, "&nf" );
                Cmd_CommandExecute( pAbcLocal, "&put" );
                // Cmd_CommandExecute( pAbcLocal, "ps" );
                // Cmd_CommandExecute( pAbcLocal, "amap" );
                // Cmd_CommandExecute( pAbcLocal, "ps" );
                Cmd_CommandExecute( pAbcLocal, "replace" );
                pNtkDup = Abc_FrameReadNtk(pAbcLocal);
                hpwlAfter = pNtkDup->globalHpwl;

                // // use GNN model to predict the HPWL-related score of the updated network
                // // Abc_NtkReassignIds( pNtkDup );
                // hpwlScoreAfter = Abc_CallGNN(pNtkTmp);
                Abc_NtkDelete(pNtkTmp); // Clean up clone

                // // generating samples for GNN model
                // if (hpwlBefore - hpwlAfter > 1)
                // {
                //     char Buffer[100];
                //     memset( Buffer, 0, sizeof(Buffer) );
                //     sprintf( Buffer, "!mkdir -p ex84/case%d", global_counter );
                //     Cmd_CommandExecute( pAbcLocal, Buffer );
                //     // Cmd_CommandExecute( pAbcLocal, "!mkdir ex84/case$global_counter" );
                //     Abc_FrameReplaceCurrentNetwork( pAbcLocal, pNtkTmp );
                //     memset( Buffer, 0, sizeof(Buffer) );
                //     sprintf( Buffer, "write_aiger ex84/case%d/a.aig", global_counter );
                //     Cmd_CommandExecute( pAbcLocal, Buffer );
                //     // Cmd_CommandExecute( pAbcLocal, "write_aiger ex84/case$global_counter/a.aig" );
                //     pNtkTmp = Abc_NtkDup(pNtk);
                //     Abc_NtkReassignIds( pNtkTmp );
                //     Abc_FrameReplaceCurrentNetwork( pAbcLocal, pNtkTmp );
                //     memset( Buffer, 0, sizeof(Buffer) );
                //     sprintf( Buffer, "write_aiger ex84/case%d/b.aig", global_counter );
                //     Cmd_CommandExecute( pAbcLocal, Buffer );
                //     // Cmd_CommandExecute( pAbcLocal, "write_aiger ex84/case$global_counter/b.aig" );
                //     memset( Buffer, 0, sizeof(Buffer) );
                //     sprintf( Buffer, "!echo 1 > ex84/case%d/label.txt", global_counter );
                //     Cmd_CommandExecute( pAbcLocal, Buffer );
                //     // Cmd_CommandExecute( pAbcLocal, "!echo 1 > ex84/case$global_counter/label.txt" );
                //     global_counter++;
                // }
                // else 
                // if (hpwlBefore - hpwlAfter < -1 && (rand() % 100) < 10)
                // {
                //     char Buffer[100];
                //     memset( Buffer, 0, sizeof(Buffer) );
                //     sprintf( Buffer, "!mkdir -p ex84/case%d", global_counter );
                //     Cmd_CommandExecute( pAbcLocal, Buffer );
                //     // Cmd_CommandExecute( pAbcLocal, "!mkdir ex84/case$global_counter" );
                //     Abc_FrameReplaceCurrentNetwork( pAbcLocal, pNtkTmp );
                //     memset( Buffer, 0, sizeof(Buffer) );
                //     sprintf( Buffer, "write_aiger ex84/case%d/a.aig", global_counter );
                //     Cmd_CommandExecute( pAbcLocal, Buffer );
                //     // Cmd_CommandExecute( pAbcLocal, "write_aiger ex84/case$global_counter/a.aig" );
                //     pNtkTmp = Abc_NtkDup(pNtk);
                //     Abc_NtkReassignIds( pNtkTmp );
                //     Abc_FrameReplaceCurrentNetwork( pAbcLocal, pNtkTmp );
                //     memset( Buffer, 0, sizeof(Buffer) );
                //     sprintf( Buffer, "write_aiger ex84/case%d/b.aig", global_counter );
                //     Cmd_CommandExecute( pAbcLocal, Buffer );
                //     // Cmd_CommandExecute( pAbcLocal, "write_aiger ex84/case$global_counter/b.aig" );
                //     memset( Buffer, 0, sizeof(Buffer) );
                //     sprintf( Buffer, "!echo 0 > ex84/case%d/label.txt", global_counter );
                //     Cmd_CommandExecute( pAbcLocal, Buffer );
                //     // Cmd_CommandExecute( pAbcLocal, "!echo 0 > ex84/case$global_counter/label.txt" );
                //     global_counter++;
                // }
                // else
                // {
                //     Abc_NtkDelete(pNtkTmp);
                // }
            }
            
            // after getting the hpwl, go back to the original Ntk
            // Abc_FrameSetCurrentNetwork( pAbc, pNtk );
            // recover the FF
            if ( ((uPhase & (1<<4)) > 0) ) Dec_GraphComplement( pGraphCur );
            // restore p->vFaninsCur to be from the original Ntk
            Vec_PtrClear( p->vFaninsCur );
            Vec_PtrFill( p->vFaninsCur, (int)pCut->nLeaves, 0 );
            for ( k = 0; k < (int)pCut->nLeaves; k++ )
            {
                pFanin = Abc_NtkObj( pNtk, pCut->pLeaves[(int)pPerm[k]] );
                if ( pFanin == NULL )
                    break;
                pFanin = Abc_ObjNotCond(pFanin, ((uPhase & (1<<k)) > 0) );
                Vec_PtrWriteEntry( p->vFaninsCur, k, pFanin );
            }

            // statistics of the GNN prediction
            // g_TotalComparisons++;
            // if ( (hpwlBefore - hpwlAfter) * (hpwlScoreAfter - hpwlScoreBefore) > 0 )
            //     g_CorrectPredictions++;
            // check if the cut is better than the current best one
            if ( hpwlBefore - hpwlAfter > 0 && GainBest < hpwlBefore - hpwlAfter )
            // if ( hpwlScoreAfter - hpwlScoreBefore > 0 && GainBest < hpwlScoreAfter - hpwlScoreBefore )
            {
                // save this form
                GainBest  = hpwlBefore - hpwlAfter;
                // GainBest  = hpwlScoreAfter - hpwlScoreBefore;
                p->pGraph  = pGraphCur;
                p->fCompl = ((uPhase & (1<<4)) > 0);
                uTruthBest = 0xFFFF & *Cut_CutReadTruth(pCut);

                // collect fanins in the
                Vec_PtrClear( p->vFanins );
                Vec_PtrForEachEntry( Abc_Obj_t *, p->vFaninsCur, pFanin, k )
                    Vec_PtrPush( p->vFanins, pFanin );
            }
        }
    }


    // if ( GainBest == -1 )
    if ( GainBest <= 0 )
        return -1;

    // copy the leaves
    Vec_PtrForEachEntry( Abc_Obj_t *, p->vFanins, pFanin, i )
        Dec_GraphNode((Dec_Graph_t *)p->pGraph, i)->pFunc = pFanin;
    
    p->nScores[p->pMap[uTruthBest]]++;
    p->nNodesGained += GainBest;
    if ( fUseZeros || GainBest > 0 )
    {
        p->nNodesRewritten++;
    }

    // report the progress
    if ( fVeryVerbose && GainBest > 0 )
    {
        printf( "Node %6s :   ", Abc_ObjName(pNode) );
        printf( "Fanins = %d. ", p->vFanins->nSize );
        printf( "Save = %d.  ", nNodesSaveCur );
        printf( "Add = %d.  ",  nNodesSaveCur-GainBest );
        printf( "GAIN = %d.  ", GainBest );
        printf( "Cone = %d.  ", p->pGraph? Dec_GraphNodeNum((Dec_Graph_t *)p->pGraph) : 0 );
        printf( "Class = %d.  ", p->pMap[uTruthBest] );
        printf( "\n" );
    }
    return GainBest;
}

/**Function*************************************************************

  Synopsis    [Evaluates the cut.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Dec_Graph_t * Rwr_CutEvaluate( Rwr_Man_t * p, Abc_Obj_t * pRoot, Cut_Cut_t * pCut, Vec_Ptr_t * vFaninsCur, int nNodesSaved, int LevelMax, int * pGainBest, int fPlaceEnable )
{
    extern int            Dec_GraphToNetworkCount( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int NodeMax, int LevelMax );
    Vec_Ptr_t * vSubgraphs;
    Dec_Graph_t * pGraphBest = NULL; // Suppress "might be used uninitialized"
    Dec_Graph_t * pGraphCur;
    Rwr_Node_t * pNode, * pFanin;
    int nNodesAdded, GainBest, i, k;
    unsigned uTruth;
    float CostBest;//, CostCur;
    // find the matching class of subgraphs
    uTruth = 0xFFFF & *Cut_CutReadTruth(pCut);
    vSubgraphs = Vec_VecEntry( p->vClasses, p->pMap[uTruth] );
    p->nSubgraphs += vSubgraphs->nSize;
    // determine the best subgraph
    GainBest = -1;
    CostBest = ABC_INFINITY;
    Vec_PtrForEachEntry( Rwr_Node_t *, vSubgraphs, pNode, i )
    {
        // get the current graph
        pGraphCur = (Dec_Graph_t *)pNode->pNext;
        // copy the leaves
        Vec_PtrForEachEntry( Rwr_Node_t *, vFaninsCur, pFanin, k )
            Dec_GraphNode(pGraphCur, k)->pFunc = pFanin;
        // detect how many unlabeled nodes will be reused
        nNodesAdded = Dec_GraphToNetworkCount( pRoot, pGraphCur, nNodesSaved, LevelMax );
        if ( nNodesAdded == -1 )
            continue;
        // assert( nNodesSaved >= nNodesAdded );
/*
        // evaluate the cut
        if ( fPlaceEnable )
        {
            extern float Abc_PlaceEvaluateCut( Abc_Obj_t * pRoot, Vec_Ptr_t * vFanins );

            float Alpha = 0.5; // ???
            float PlaceCost;

            // get the placement cost of the cut
            PlaceCost = Abc_PlaceEvaluateCut( pRoot, vFaninsCur );

            // get the weigted cost of the cut
            CostCur = nNodesSaved - nNodesAdded + Alpha * PlaceCost;

            // do not allow uphill moves
            if ( nNodesSaved - nNodesAdded < 0 )
                continue;

            // decide what cut to use
            if ( CostBest > CostCur )
            {
                GainBest   = nNodesSaved - nNodesAdded; // pure node cost
                CostBest   = CostCur;                   // cost with placement
                pGraphBest = pGraphCur;                 // subgraph to be used for rewriting

                // score the graph
                if ( nNodesSaved - nNodesAdded > 0 )
                {
                    pNode->nScore++;
                    pNode->nGain += GainBest;
                    pNode->nAdded += nNodesAdded;
                }
            }
        }
        else
*/
        {
            // count the gain at this node
            if ( GainBest < nNodesSaved - nNodesAdded )
            {
                GainBest   = nNodesSaved - nNodesAdded;
                pGraphBest = pGraphCur;

                // score the graph
                if ( nNodesSaved - nNodesAdded > 0 )
                {
                    pNode->nScore++;
                    pNode->nGain += GainBest;
                    pNode->nAdded += nNodesAdded;
                }
            }
        }
    }
    if ( GainBest == -1 )
        return NULL;
    *pGainBest = GainBest;
    return pGraphBest;
}

Dec_Graph_t *   Rwr_CutEvaluateP( Rwr_Man_t * p, Abc_Obj_t * pRoot, Cut_Cut_t * pCut, Vec_Ptr_t * vFaninsCur, int nNodesSaved, float HPWLSaved, float overflowBefore, float overflowAfter, int LevelMax, float * pGainBest, int fPlaceEnable, int fSimple, int fClustering )
{
    extern int            Dec_GraphToNetworkCount( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int NodeMax, int LevelMax );
    extern float          Dec_GraphToNetworkHPWL( Abc_Obj_t * pRoot, Dec_Graph_t * pGraph, int LevelMax, float overflow, float overflowBefore, float overflowAfter, int fSimple, int fClustering,
        int * pRootArrivalT, int * pRootReqT, float * pRootArrivalG, float * pRootReqG );
    Vec_Ptr_t * vSubgraphs;
    Dec_Graph_t * pGraphBest = NULL; // Suppress "might be used uninitialized"
    Dec_Graph_t * pGraphCur;
    Rwr_Node_t * pNode, * pFanin;
    int nNodesAdded = 0, i, k;
    float HPWLGainBest;
    int nNodesGainBest;
    float HPWLAdded = 0.0, HPWLSavedEval;
    unsigned uTruth;
    float ScoreBest = 0.0, ScoreCur;
    int RootArrivalT, RootReqT, SlackTCur, SlackTNew;
    float RootArrivalG, RootReqG, SlackGCur, SlackGNew;
    float GScale, TimingPartBase, TimingPartCur, HpwlPartCur, MinSlackNorm, MinSlackNormBase;
    float AlphaHpwl = 0.2f;
    // find the matching class of subgraphs
    uTruth = 0xFFFF & *Cut_CutReadTruth(pCut);
    vSubgraphs = Vec_VecEntry( p->vClasses, p->pMap[uTruth] );
    p->nSubgraphs += vSubgraphs->nSize;
    // determine the best subgraph
    HPWLGainBest = -1;
    nNodesGainBest = -1;
    GScale = pRoot->pNtk->timingGScale > 1.0e-4f ? pRoot->pNtk->timingGScale : 1.0f;
    SlackTCur = pRoot->reqT - pRoot->arrivalT;
    SlackGCur = pRoot->reqG - pRoot->arrivalG;
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
    ScoreBest = 0.0f;

    Vec_PtrForEachEntry( Rwr_Node_t *, vSubgraphs, pNode, i )
    {
        // get the current graph
        pGraphCur = (Dec_Graph_t *)pNode->pNext;
        // copy the leaves
        Vec_PtrForEachEntry( Rwr_Node_t *, vFaninsCur, pFanin, k )
            Dec_GraphNode(pGraphCur, k)->pFunc = pFanin;
        // detect how many unlabeled nodes will be reused
        nNodesAdded = Dec_GraphToNetworkCount( pRoot, pGraphCur, nNodesSaved, LevelMax );
        // doesn't allow count increase
        if ( nNodesAdded == -1 )
            continue;
        // // prioritize node counts optimization
        // if ( nNodesGainBest > nNodesSaved - nNodesAdded )
        //     continue;

        // if ( nNodesGainBest > -1 && nNodesGainBest < nNodesSaved - nNodesAdded )
        // {
        //     nNodesGainBest = nNodesSaved - nNodesAdded;
        //     HPWLGainBest = HPWLSaved - HPWLAdded;
        //     pGraphBest = pGraphCur;
        //     printf("########%d, %d, %.2f, %.2f\n", nNodesSaved, nNodesAdded, HPWLSaved, HPWLAdded);
        //     continue;
        // }
        HPWLAdded = Dec_GraphToNetworkHPWL( pRoot, pGraphCur, LevelMax, 0, 0, 0, fSimple, fClustering,
            &RootArrivalT, &RootReqT, &RootArrivalG, &RootReqG );//overflow, overflowBefore, overflowAfter );
        if ( HPWLAdded == -1 )
            continue;
        SlackTNew = RootReqT - RootArrivalT;
        SlackGNew = RootReqG - RootArrivalG;
        MinSlackNorm = Abc_MinFloat( (float)SlackTNew, SlackGNew / GScale );
        if ( MinSlackNorm < -0.5f )
            continue;
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

        if ( ScoreBest < ScoreCur )
        {
            HPWLGainBest = HPWLSaved - HPWLAdded;
            nNodesGainBest = nNodesSaved - nNodesAdded;
            pGraphBest = pGraphCur;
            ScoreBest = ScoreCur;
        }
    }
    // if ( nNodesGainBest == -1 )
    //     return NULL;
    if ( pGraphBest == NULL )
        return NULL;
    // prioritize node counts optimization
    // *pGainBest = nNodesGainBest * 10000 + HPWLGainBest;
    *pGainBest = ScoreBest;
    return pGraphBest;
}
/**Function*************************************************************

  Synopsis    [Checks the type of the cut.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Rwr_CutIsBoolean_rec( Abc_Obj_t * pObj, Vec_Ptr_t * vLeaves, int fMarkA )
{
    if ( Vec_PtrFind(vLeaves, pObj) >= 0 || Vec_PtrFind(vLeaves, Abc_ObjNot(pObj)) >= 0 )
    {
        if ( fMarkA )
            pObj->fMarkA = 1;
        else
            pObj->fMarkB = 1;
        return;
    }
    assert( !Abc_ObjIsCi(pObj) );
    Rwr_CutIsBoolean_rec( Abc_ObjFanin0(pObj), vLeaves, fMarkA );
    Rwr_CutIsBoolean_rec( Abc_ObjFanin1(pObj), vLeaves, fMarkA );
}

/**Function*************************************************************

  Synopsis    [Checks the type of the cut.]

  Description [Returns 1(0) if the cut is Boolean (algebraic).]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Rwr_CutIsBoolean( Abc_Obj_t * pObj, Vec_Ptr_t * vLeaves )
{
    Abc_Obj_t * pTemp;
    int i, RetValue;
    Vec_PtrForEachEntry( Abc_Obj_t *, vLeaves, pTemp, i )
    {
        pTemp = Abc_ObjRegular(pTemp);
        assert( !pTemp->fMarkA && !pTemp->fMarkB );
    }
    Rwr_CutIsBoolean_rec( Abc_ObjFanin0(pObj), vLeaves, 1 );
    Rwr_CutIsBoolean_rec( Abc_ObjFanin1(pObj), vLeaves, 0 );
    RetValue = 0;
    Vec_PtrForEachEntry( Abc_Obj_t *, vLeaves, pTemp, i )
    {
        pTemp = Abc_ObjRegular(pTemp);
        RetValue |= pTemp->fMarkA && pTemp->fMarkB;
        pTemp->fMarkA = pTemp->fMarkB = 0;
    }
    return RetValue;
}


/**Function*************************************************************

  Synopsis    [Count the nodes in the cut space of a node.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Rwr_CutCountNumNodes_rec( Abc_Obj_t * pObj, Cut_Cut_t * pCut, Vec_Ptr_t * vNodes )
{
    int i;
    for ( i = 0; i < (int)pCut->nLeaves; i++ )
        if ( pCut->pLeaves[i] == pObj->Id )
        {
            // check if the node is collected
            if ( pObj->fMarkC == 0 )
            {
                pObj->fMarkC = 1;
                Vec_PtrPush( vNodes, pObj );
            }
            return;
        }
    assert( Abc_ObjIsNode(pObj) );
    // check if the node is collected
    if ( pObj->fMarkC == 0 )
    {
        pObj->fMarkC = 1;
        Vec_PtrPush( vNodes, pObj );
    }
    // traverse the fanins
    Rwr_CutCountNumNodes_rec( Abc_ObjFanin0(pObj), pCut, vNodes );
    Rwr_CutCountNumNodes_rec( Abc_ObjFanin1(pObj), pCut, vNodes );
}

/**Function*************************************************************

  Synopsis    [Count the nodes in the cut space of a node.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Rwr_CutCountNumNodes( Abc_Obj_t * pObj, Cut_Cut_t * pCut )
{
    Vec_Ptr_t * vNodes;
    int i, Counter;
    // collect all nodes
    vNodes = Vec_PtrAlloc( 100 );
    for ( pCut = pCut->pNext; pCut; pCut = pCut->pNext )
        Rwr_CutCountNumNodes_rec( pObj, pCut, vNodes );
    // clean all nodes
    Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pObj, i )
        pObj->fMarkC = 0;
    // delete and return
    Counter = Vec_PtrSize(vNodes);
    Vec_PtrFree( vNodes );
    return Counter;
}


/**Function*************************************************************

  Synopsis    [Returns depth of the cut.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Rwr_NodeGetDepth_rec( Abc_Obj_t * pObj, Vec_Ptr_t * vLeaves )
{
    Abc_Obj_t * pLeaf;
    int i, Depth0, Depth1;
    if ( Abc_ObjIsCi(pObj) )
        return 0;
    Vec_PtrForEachEntry( Abc_Obj_t *, vLeaves, pLeaf, i )
        if ( pObj == Abc_ObjRegular(pLeaf) )
            return 0;
    Depth0 = Rwr_NodeGetDepth_rec( Abc_ObjFanin0(pObj), vLeaves );
    Depth1 = Rwr_NodeGetDepth_rec( Abc_ObjFanin1(pObj), vLeaves );
    return 1 + Abc_MaxInt( Depth0, Depth1 );
}


/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Rwr_ScoresClean( Rwr_Man_t * p )
{
    Vec_Ptr_t * vSubgraphs;
    Rwr_Node_t * pNode;
    int i, k;
    for ( i = 0; i < p->vClasses->nSize; i++ )
    {
        vSubgraphs = Vec_VecEntry( p->vClasses, i );
        Vec_PtrForEachEntry( Rwr_Node_t *, vSubgraphs, pNode, k )
            pNode->nScore = pNode->nGain = pNode->nAdded = 0;
    }
}

static int Gains[222];

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Rwr_ScoresCompare( int * pNum1, int * pNum2 )
{
    if ( Gains[*pNum1] > Gains[*pNum2] )
        return -1;
    if ( Gains[*pNum1] < Gains[*pNum2] )
        return 1;
    return 0;
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Rwr_ScoresReport( Rwr_Man_t * p )
{
    extern void Ivy_TruthDsdComputePrint( unsigned uTruth );
    int Perm[222];
    Vec_Ptr_t * vSubgraphs;
    Rwr_Node_t * pNode;
    int i, iNew, k;
    unsigned uTruth;
    // collect total gains
    assert( p->vClasses->nSize == 222 );
    for ( i = 0; i < p->vClasses->nSize; i++ )
    {
        Perm[i] = i;
        Gains[i] = 0;
        vSubgraphs = Vec_VecEntry( p->vClasses, i );
        Vec_PtrForEachEntry( Rwr_Node_t *, vSubgraphs, pNode, k )
            Gains[i] += pNode->nGain;
    }
    // sort the gains
    qsort( Perm, (size_t)222, sizeof(int), (int (*)(const void *, const void *))Rwr_ScoresCompare );

    // print classes
    for ( i = 0; i < p->vClasses->nSize; i++ )
    {
        iNew = Perm[i];
        if ( Gains[iNew] == 0 )
            break;
        vSubgraphs = Vec_VecEntry( p->vClasses, iNew );
        printf( "CLASS %3d: Subgr = %3d. Total gain = %6d.  ", iNew, Vec_PtrSize(vSubgraphs), Gains[iNew] );
        uTruth = (unsigned)p->pMapInv[iNew];
        Extra_PrintBinary( stdout, &uTruth, 16 );
        printf( "  " );
        Ivy_TruthDsdComputePrint( (unsigned)p->pMapInv[iNew] | ((unsigned)p->pMapInv[iNew] << 16) );
        Vec_PtrForEachEntry( Rwr_Node_t *, vSubgraphs, pNode, k )
        {
            if ( pNode->nScore == 0 )
                continue;
            printf( "    %2d: S=%5d. A=%5d. G=%6d. ", k, pNode->nScore, pNode->nAdded, pNode->nGain );
            Dec_GraphPrint( stdout, (Dec_Graph_t *)pNode->pNext, NULL, NULL );
        }
    }
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END
