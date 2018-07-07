/******************************************************************************
* Author: Christopher Dubbs
* Date: May 15, 2018
* CS475
* Program #: 6
* Program Name: OpenCL Array Multiplication, Multiply-Add, and Multiply-Reduce (Part 2)
******************************************************************************/
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <omp.h>
#include <iostream>
#include <iomanip>
#include <fstream>

#include "cl.h"
#include "cl_platform.h"


#ifndef NUM_ELEMENTS
#define	NUM_ELEMENTS		64*1024*1024
#endif

#ifndef LOCAL_SIZE
#define	LOCAL_SIZE		32
#endif

#define	NUM_WORK_GROUPS		NUM_ELEMENTS/LOCAL_SIZE		//i.e. compute units

const char *		CL_FILE_NAME = { "multiplyReduce.cl" };	// Specify kernal file
const float			TOL = 0.0001f;

void				Wait( cl_command_queue );
int				LookAtTheBits( float );

void printResults(double GigaMultsPerSecond, double sum);

int
main( int argc, char *argv[ ] )
{
	// see if we can even open the opencl kernel program
	// (no point going on if we can't):

	FILE *fp;
#ifdef WIN32
	errno_t err = fopen_s( &fp, CL_FILE_NAME, "r" );
	if( err != 0 )
#else
	fp = fopen( CL_FILE_NAME, "r" );
	if( fp == NULL )
#endif
	{
		fprintf( stderr, "Cannot open OpenCL source file '%s'\n", CL_FILE_NAME );
		return 1;
	}

	cl_int status;		// returned status from opencl calls
				// test against CL_SUCCESS

	// get the platform id:

	cl_platform_id platform;
	status = clGetPlatformIDs( 1, &platform, NULL );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clGetPlatformIDs failed (2)\n" );
	
	// get the device id:

	cl_device_id device;
	status = clGetDeviceIDs( platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clGetDeviceIDs failed (2)\n" );

	// 2. allocate the host memory buffers:

	float *hA = new float[ NUM_ELEMENTS ];
	float *hB = new float[ NUM_ELEMENTS ];
	float *hC = new float[ NUM_WORK_GROUPS ];

	// fill the host memory buffers:

	for( int i = 0; i < NUM_ELEMENTS; i++ )
	{
		//Simplify error check for reduction
		hA[i] = hB[i] = 1.;
		//hA[i] = hB[i] = (float) sqrt(  (double)i  );
	}

	//size_t dataSize = NUM_ELEMENTS * sizeof(float);	// Not used values set manually per buffer use

	// 3. create an opencl context:

	cl_context context = clCreateContext( NULL, 1, &device, NULL, NULL, &status );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clCreateContext failed\n" );

	// 4. create an opencl command queue:

	cl_command_queue cmdQueue = clCreateCommandQueue( context, device, 0, &status );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clCreateCommandQueue failed\n" );

	
	// 5. allocate the device memory buffers:

	cl_mem dA = clCreateBuffer( context, CL_MEM_READ_ONLY,  NUM_ELEMENTS * sizeof(float), NULL, &status );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clCreateBuffer failed (1)\n" );

	cl_mem dB = clCreateBuffer( context, CL_MEM_READ_ONLY,  NUM_ELEMENTS * sizeof(float), NULL, &status );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clCreateBuffer failed (2)\n" );

	cl_mem dC = clCreateBuffer( context, CL_MEM_WRITE_ONLY, NUM_WORK_GROUPS *sizeof(float), NULL, &status );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clCreateBuffer failed (3)\n" );


	// 6. enqueue the 3 commands to write the data from the host buffers to the device buffers:

	status = clEnqueueWriteBuffer( cmdQueue, dA, CL_FALSE, 0, NUM_ELEMENTS * sizeof(float), hA, 0, NULL, NULL );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clEnqueueWriteBuffer failed (1)\n" );

	status = clEnqueueWriteBuffer( cmdQueue, dB, CL_FALSE, 0, NUM_ELEMENTS * sizeof(float), hB, 0, NULL, NULL );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clEnqueueWriteBuffer failed (2)\n" );



	Wait( cmdQueue );

	// 7. read the kernel code from a file:

	fseek( fp, 0, SEEK_END );
	size_t fileSize = ftell( fp );
	fseek( fp, 0, SEEK_SET );
	char *clProgramText = new char[ fileSize+1 ];		// leave room for '\0'
	size_t n = fread( clProgramText, 1, fileSize, fp );
	clProgramText[fileSize] = '\0';
	fclose( fp );
	if( n != fileSize )
		fprintf( stderr, "Expected to read %d bytes read from '%s' -- actually read %d.\n", fileSize, CL_FILE_NAME, n );

	// create the text for the kernel program:

	char *strings[1];
	strings[0] = clProgramText;
	cl_program program = clCreateProgramWithSource( context, 1, (const char **)strings, NULL, &status );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clCreateProgramWithSource failed\n" );
	delete [ ] clProgramText;

	// 8. compile and link the kernel code:

	char *options = { "" };
	status = clBuildProgram( program, 1, &device, options, NULL, NULL );
	if( status != CL_SUCCESS )
	{
		size_t size;
		clGetProgramBuildInfo( program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &size );
		cl_char *log = new cl_char[ size ];
		clGetProgramBuildInfo( program, device, CL_PROGRAM_BUILD_LOG, size, log, NULL );
		fprintf( stderr, "clBuildProgram failed:\n%s\n", log );
		delete [ ] log;
	}

	// 9. create the kernel object:

	cl_kernel kernel = clCreateKernel( program, "ArrayMultReduce", &status );  
	if( status != CL_SUCCESS )
		fprintf( stderr, "clCreateKernel failed\n" );

	// 10. setup the arguments to the kernel object:

	status = clSetKernelArg( kernel, 0, sizeof(cl_mem), &dA );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clSetKernelArg failed (1)\n" );

	status = clSetKernelArg( kernel, 1, sizeof(cl_mem), &dB );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clSetKernelArg failed (2)\n" );

	status = clSetKernelArg( kernel, 2, sizeof(float), NULL );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clSetKernelArg failed (2)\n" );

	status = clSetKernelArg( kernel, 3, sizeof(cl_mem), &dC );	
	if( status != CL_SUCCESS )
		fprintf( stderr, "clSetKernelArg failed (3)\n" );


	// 11. enqueue the kernel object for execution:

	size_t globalWorkSize[3] = { NUM_ELEMENTS, 1, 1 };	// GLOBAL SIZE
	size_t localWorkSize[3]  = { LOCAL_SIZE,   1, 1 };	// LOCAL SIZE

	Wait( cmdQueue );

	double time0 = omp_get_wtime( );	// Start Clock

	status = clEnqueueNDRangeKernel( cmdQueue, kernel, 1, NULL, globalWorkSize, localWorkSize, 0, NULL, NULL );
	if( status != CL_SUCCESS )
		fprintf( stderr, "clEnqueueNDRangeKernel failed: %d\n", status );

	Wait( cmdQueue );

	double time1 = omp_get_wtime( );	// Stop Clock

	// 12. read the results buffer back from the device to the host:

	status = clEnqueueReadBuffer( cmdQueue, dC, CL_TRUE, 0, NUM_WORK_GROUPS*sizeof(float), hC, 0, NULL, NULL );
	if( status != CL_SUCCESS )
			fprintf( stderr, "clEnqueueReadBuffer failed\n" );

	Wait( cmdQueue );

	// Add elements of returned array to validate results 
	// (sum should equal global size, given that hA and hB are filled with a value of 1)
	double sum = 0;
	std::cout << std::endl;
	for(int i = 0; i < NUM_WORK_GROUPS; i++)
	{
		sum += hC[i];
	}
	std::cout << std::endl;



	
	// OUTPUT RESULTS TO TERMINAL AND FILE
	double GigaMultsPerSecond = (double)NUM_ELEMENTS/(time1-time0)/1000000000.;

	printResults(GigaMultsPerSecond, sum);

	/*
	fprintf( stderr, "%8d\t%4d\t%10d\t%10.3lf GigaMultsPerSecond\n",
		NUM_ELEMENTS, LOCAL_SIZE, NUM_WORK_GROUPS, (double)NUM_ELEMENTS/(time1-time0)/1000000000. );
	*/

	/*
	// did it work?

	for( int i = 0; i < NUM_ELEMENTS; i++ )
	{
		float expected = hA[i] * hB[i];
		if( fabs( hC[i] - expected ) > TOL )
		{
			fprintf( stderr, "%4d: %13.6f * %13.6f wrongly produced %13.6f instead of %13.6f (%13.8f)\n",
				i, hA[i], hB[i], hC[i], expected, fabs(hC[i]-expected) );
			fprintf( stderr, "%4d:    0x%08x *    0x%08x wrongly produced    0x%08x instead of    0x%08x\n",
				i, LookAtTheBits(hA[i]), LookAtTheBits(hB[i]), LookAtTheBits(hC[i]), LookAtTheBits(expected) );
		}
	}*/

	

#ifdef WIN32
	Sleep( 2000 );
#endif


	// 13. clean everything up:

	clReleaseKernel(        kernel   );
	clReleaseProgram(       program  );
	clReleaseCommandQueue(  cmdQueue );
	clReleaseMemObject(     dA  );
	clReleaseMemObject(     dB  );
	clReleaseMemObject(     dC  );

	delete [ ] hA;
	delete [ ] hB;
	delete [ ] hC;
	return 0;
}


// wait until all queued tasks have completed:

void
Wait( cl_command_queue queue )
{
	cl_event wait;

	cl_int status = clEnqueueMarker( queue, &wait );
	if( status != CL_SUCCESS )
		fprintf( stderr, "Wait: clEnqueueMarker failed\n" );

	status = clWaitForEvents( 1, &wait );		// Per discussion board post cited in header
	if( status != CL_SUCCESS )
		fprintf( stderr, "Wait: clEnqueueWaitForEvents failed\n" ); 
}


int
LookAtTheBits( float fp )
{
	int *ip = (int *)&fp;
	return *ip;
}


/******************************************************************************
* Function Name: printResults
******************************************************************************/
void printResults(double GigaMultsPerSecond, double sum)
{
    // Open file to print results
	std::ofstream p2Results;
	p2Results.open("p2Results.txt", std::ios::out | std::ios::app);

    

    // PRINT HEADINGS
    if(NUM_ELEMENTS == 1024)
    {
        
        std::cout << "\n-----------------------------------------------------\n";
        p2Results << "\n-----------------------------------------------------\n";
        std::cout << "|                   MULTIPLY REDUCE                 |" << std::endl; 
        p2Results << "|                   MULTIPLY REDUCE                 |" << std::endl;
        std::cout << "-----------------------------------------------------\n";
        p2Results << "-----------------------------------------------------\n";
        

        std::cout << std::left << std::setw(24) << "LOCAL_SIZE ";
        p2Results << std::left << std::setw(24) << "LOCAL_SIZE ";
        std::cout << std::left << std::setw(24) << "GLOBAL_SIZE";
        p2Results << std::left << std::setw(24) << "GLOBAL_SIZE";
        std::cout << std::left << std::setw(24) << "NUM_WORK_GROUPS";
        p2Results << std::left << std::setw(24) << "NUM_WORK_GROUPS";
        std::cout << std::left << std::setw(24) << "GigaMultsPerSecond ";
        p2Results << std::left << std::setw(24) << "GigaMultsPerSecond";
        std::cout << std::left << std::setw(24) << "SUM";
        p2Results << std::left << std::setw(24) << "SUM"; 
        std::cout << std::endl;
        p2Results << std::endl;
    }

    std::cout << std::fixed << std::setprecision(10);
    p2Results << std::fixed  << std::setprecision(10);

    // PRINT RESULTS

    std::cout << std::left << std::setw(24) << LOCAL_SIZE;
    p2Results << std::left << std::setw(24) << LOCAL_SIZE; 
    std::cout << std::left << std::setw(24) << NUM_ELEMENTS;
    p2Results << std::left << std::setw(24) << NUM_ELEMENTS;
    std::cout << std::left << std::setw(24) << NUM_WORK_GROUPS;
    p2Results << std::left << std::setw(24) << NUM_WORK_GROUPS;
    std::cout << std::left << std::setw(24) << GigaMultsPerSecond;
    p2Results << std::left << std::setw(24) << GigaMultsPerSecond;
    std::cout << std::fixed << std::setprecision(2);
    p2Results << std::fixed  << std::setprecision(2);
    std::cout << std::left << std::setw(24) << sum;
    p2Results << std::left << std::setw(24) << sum;
    std::cout << std::endl;
    p2Results << std::endl; 

    p2Results.close();
}
