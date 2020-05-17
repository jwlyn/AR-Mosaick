#ifndef _HSVHIST_H
#define _HSVHIST_H

#include <opencv2/opencv.hpp>
using namespace cv;

/* number of bins of HSV in histogram */
#define NH 10
#define NS 10
#define NV 10

/* max HSV values */
#define H_MAX 360.0
#define S_MAX 1.0
#define V_MAX 1.0
/* low thresholds on saturation and value for histogramming */
#define S_THRESH 0.1
#define V_THRESH 0.2

/********************************* Structures *********************************/

/**
An HSV histogram represented by NH * NS + NV bins.  Pixels with saturation
and value greater than S_THRESH and V_THRESH fill the first NH * NS bins.
Other, "colorless" pixels fill the last NV value-only bins.
*/
typedef struct histogram {
	float histo[NH*NS + NV];   /**< histogram array */
	int n;                     /**< length of histogram array */
} histogram;




void normalize_histogram(histogram* histo);
float histo_dist_sq(histogram* h1, histogram* h2);
//histogram* calc_histogram(Mat imgs);
bool calc_histogram(Mat imgs, histogram* histo);
Mat img2hsv(Mat image, Rect roi);
#endif