/**
 * @file v4.cpp
 * @author GKC_NKCS (2012522@mail.nankai.edu.cn)
 * @brief
 * @version 0.1
 * @date 2022-07-01
 *
 * @copyright Copyright (c) 2022
 * @details mainbody of MPI gauss elimination pipline non-blocking communication
 *
 */
#include <stdio.h>
#include "mpi.h"
#include <string>
#include "file.h"
#include "bitmap.h"

int myid;         // rank of current processor
int numprocs;     // number of processor
double s_time;    // start time
double e_time;    // end time
int *eliminatant; // eliminatant wnd
int *eliminator;  // eliminatant wnd
int *sub;         // task assigned to each processor
int wndSize;      // max cols
int wndSize1;     // rows of eliminatant wnd
int wndSize2;     // rows of eliminator wnd
int n_wndSize1;   // new rows of eliminatant
int np;           // rows of sub
int wrdLen;       // cols per row
BitManager *eliminatantManager;
BitManager *eliminatorManager;
BitManager *subManager;

// string basePath = "F:/大二下课程/并行计算/期末研究报告相关材料/data/Groebner/";
string basePath = "/home/bill/Desktop/para/src/Groebner/";
string examplePath = basePath + getExampleName(7);

void init();
void broadcast();
void gaussian();
void write();

int main(int argc, char *argv[])
{
    getParam(examplePath, wndSize1, wndSize2, wndSize); // get size of wnd
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &myid);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    if (myid == 0)
        s_time = MPI_Wtime(); // start timing

    /* init wnd and relavant params */
    init();

    /* broadcast task */
    broadcast();

    /* conduct elimination */
    gaussian();

    /*  gather and output result */
    write();

    if (myid == 0) // end timing
    {
        e_time = MPI_Wtime();
        cout << "time: " << e_time - s_time << endl;
    }

    MPI_Finalize();
    return 0;
}

void init()
{
    n_wndSize1 = wndSize1 % numprocs == 0 ? wndSize1 : wndSize1 + (numprocs - wndSize1 % numprocs);
    wrdLen = wndSize / WORD_BITS + 1;
    wrdLen += wrdLen % INDEX_BLOCK_SIZE == 0 ? 0 : INDEX_BLOCK_SIZE - (wrdLen % INDEX_BLOCK_SIZE);
    eliminatant = new int[(long long)n_wndSize1 * wrdLen]{0};
    eliminator = new int[(long long)wndSize * wrdLen]{0};
    if (myid == 0)
    {
        string *eliminatantSparseWnd = new string[n_wndSize1];
        getSparseMatrix(examplePath, eliminatantSparseWnd, n_wndSize1, ELIMINATANT);
        createWnd(eliminatantSparseWnd, eliminatant, n_wndSize1, wrdLen, UNORDERED);
        delete[] eliminatantSparseWnd;
        eliminatantSparseWnd = nullptr;
    }
    string *eliminatorSparseWnd = new string[wndSize2];
    getSparseMatrix(examplePath, eliminatorSparseWnd, wndSize2, ELIMINATOR);
    createWnd(eliminatorSparseWnd, eliminator, wndSize2, wrdLen, ORDERED);
    delete[] eliminatorSparseWnd;
    eliminatorSparseWnd = nullptr;
}

void broadcast()
{
    np = n_wndSize1 / numprocs;
    sub = new int[(long long)np * wrdLen]{0};
    MPI_Scatter(eliminatant, np * wrdLen, MPI_INT, sub, np * wrdLen, MPI_INT, 0, MPI_COMM_WORLD);
}

void gaussian()
{
    MPI_Status status;
    MPI_Request request;
    subManager = new BitManager[np];
    eliminatorManager = new BitManager[wndSize];
    buildBitManager(eliminator, wrdLen, eliminatorManager, wndSize);
    buildBitManager(sub, wrdLen, subManager, np);
    int *tmp = new int[wrdLen]{0};

    for (int i = 0; i < myid * np; i++)
    {
        // Get data from last level of pipeline
        MPI_Recv(tmp, wrdLen, MPI_INT, myid - 1, 0, MPI_COMM_WORLD, &status);

        // send data to next level of pipeline
        if (myid != (numprocs - 1))
        {
            MPI_Isend(tmp, wrdLen, MPI_INT, myid + 1, 0, MPI_COMM_WORLD, &request);
        }
        BitManager tmpManager;
        buildBitManager(tmp, wrdLen, &tmpManager);
        if (tmpManager.lftCol != -1 && eliminatorManager[tmpManager.lftCol].lftCol == -1)
        {
            copyBitMap(tmp, eliminator + tmpManager.lftCol * wrdLen, &tmpManager, eliminatorManager + tmpManager.lftCol);
        }
        // eliminate individually
        for (int row = 0; row < np; row++)
        {
            // currently, i equals k, standing for the rank of elimination
            while (subManager[row].lftCol != -1 && eliminatorManager[subManager[row].lftCol].lftCol != -1)
            {
                xorBitmap(sub + wrdLen * row, eliminator + wrdLen * subManager[row].lftCol, subManager + row, eliminatorManager + subManager[row].lftCol);
            }
        }
        if (myid != (numprocs - 1))
        {
            MPI_Wait(&request, MPI_STATUS_IGNORE);
        }
    }
    for (int row = 0; row < np; row++)
    {
        // get the last result from the last processor
        if (row == 0 && myid != 0)
        {
            MPI_Recv(tmp, wrdLen, MPI_INT, myid - 1, 0, MPI_COMM_WORLD, &status);
        }
        // send result to next pipeline
        if (myid != (numprocs - 1))
        {
            MPI_Isend(tmp, wrdLen, MPI_INT, myid + 1, 0, MPI_COMM_WORLD, &request); // can be non-blocking
        }
        BitManager tmpManager;
        buildBitManager(tmp, wrdLen, &tmpManager);
        if (tmpManager.lftCol != -1 && eliminatorManager[tmpManager.lftCol].lftCol == -1)
        {
            copyBitMap(tmp, eliminator + tmpManager.lftCol * wrdLen, &tmpManager, eliminatorManager + tmpManager.lftCol);
        }
        while (subManager[row].lftCol != -1 && eliminatorManager[subManager[row].lftCol].lftCol != -1)
        {
            xorBitmap(sub + wrdLen * row, eliminator + wrdLen * subManager[row].lftCol, subManager + row, eliminatorManager + subManager[row].lftCol);
        }
        if (myid != (numprocs - 1))
        {
            MPI_Wait(&request, MPI_STATUS_IGNORE);
        }
        copyBitmapSingle(sub + row * wrdLen, tmp, wrdLen);
        // ensure the the next processor have tmp initialized
        if (row == np - 1 && myid != numprocs - 1)
        {
            MPI_Send(tmp, wrdLen, MPI_INT, myid + 1, 0, MPI_COMM_WORLD); // must be blocking
        }
    }
}

void write()
{
    MPI_Gather(sub, wrdLen * np, MPI_INT, eliminatant, np * wrdLen, MPI_INT, 0, MPI_COMM_WORLD);
    if (myid == 0)
    {
        string *result = new string[wndSize1];
        toString(eliminatant, wrdLen, result, wndSize1);
        writeResult(examplePath, result, wndSize1);
    }
}