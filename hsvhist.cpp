#include "hsvhist.h"
#include "recttools.hpp"

using namespace cv;

/*
Calculates the histogram bin into which an HSV entry falls

@param h Hue
@param s Saturation
@param v Value

@return Returns the bin index corresponding to the HSV color defined by
\a h, \a s, and \a v.
*/
int histo_bin(float h, float s, float v)
{
	int hd, sd, vd;

	/* if S or V is less than its threshold, return a "colorless" bin */
	vd = MIN((int)(v * NV / V_MAX), NV - 1);
	if (s < S_THRESH || v < V_THRESH)
		return NH * NS + vd;

	/* otherwise determine "colorful" bin */
	hd = MIN((int)(h * NH / H_MAX), NH - 1);
	sd = MIN((int)(s * NS / S_MAX), NS - 1);
	return sd * NH + hd;
}


/*
Calculates a cumulative histogram as defined above for a given array
of images

@param img an array of images over which to compute a cumulative histogram;
each must have been converted to HSV colorspace using bgr2hsv()
@param n the number of images in imgs

@return Returns an un-normalized HSV histogram for \a imgs
*/
bool calc_histogram(Mat imgs,histogram* histo)
{
	//Mat* img;
	//histogram* histo;
	Mat h, s, v;
	float* hist;
	int i, r, c, bin;
	
	if((!histo)||(imgs.empty()))
	{
		return false;
	}

	histo->n = NH*NS + NV;
	hist = histo->histo;
	memset(hist, 0, histo->n * sizeof(float));

	/* extract individual HSV planes from image */
	Mat hsv[] = { Mat::zeros(imgs.size(), CV_32F), Mat::zeros(imgs.size(), CV_32F), Mat::zeros(imgs.size(), CV_32F) };
	cv::split(imgs, hsv);

	/* increment appropriate histogram bin for each pixel */
	float *h_data = (float*)hsv[0].data;
	float *s_data = (float*)hsv[1].data;
	float *v_data = (float*)hsv[2].data;
	for (r = 0; r < imgs.rows; r++)
	for (c = 0; c < imgs.cols; c++)
	{
	/*	bin = histo_bin(*(h_data + r * imgs.cols + c),
			*(s_data + r * imgs.cols + c),
			*(v_data + r * imgs.cols + c));*/
		bin = histo_bin(*h_data, *s_data, *v_data);
		h_data++;
		s_data++;
		v_data++;
		hist[bin] += 1;
	}

	return true;
}



#if 0


/*
Calculates a cumulative histogram as defined above for a given array
of images

@param img an array of images over which to compute a cumulative histogram;
each must have been converted to HSV colorspace using bgr2hsv()
@param n the number of images in imgs

@return Returns an un-normalized HSV histogram for \a imgs
*/
histogram* calc_histogram1(IplImage* imgs)
{
	IplImage* img;
	histogram* histo;
	IplImage* h, *s, *v;
	float* hist;
	int i, r, c, bin;

	histo = (histogram*)malloc(sizeof(histogram));
	histo->n = NH*NS + NV;
	hist = histo->histo;
	memset(hist, 0, histo->n * sizeof(float));

	/* extract individual HSV planes from image */
	img = imgs;
	h = cvCreateImage(cvGetSize(img), IPL_DEPTH_32F, 1);
	s = cvCreateImage(cvGetSize(img), IPL_DEPTH_32F, 1);
	v = cvCreateImage(cvGetSize(img), IPL_DEPTH_32F, 1);
	cvSplit(img, h, s, v, NULL);

	/* increment appropriate histogram bin for each pixel */
	for (r = 0; r < img->height; r++)
	for (c = 0; c < img->width; c++)
	{
		bin = histo_bin( /*pixval32f( h, r, c )*/((float*)(h->imageData + h->widthStep*r))[c],
			((float*)(s->imageData + s->widthStep*r))[c],
			((float*)(v->imageData + v->widthStep*r))[c]);
		hist[bin] += 1;
	}
	cvReleaseImage(&h);
	cvReleaseImage(&s);
	cvReleaseImage(&v);
	return histo;
}
#endif
void normalize_histogram(histogram* histo)
{
	float* hist;
	float sum = 0, inv_sum;
	int i, n;

	if (!histo)
	{
		return ;
	}
	hist = histo->histo;
	n = histo->n;

	/* compute sum of all bins and multiply each bin by the sum's inverse */
	for (i = 0; i < n; i++)
		sum += hist[i];
	inv_sum = 1.0 / sum;
	for (i = 0; i < n; i++)
		hist[i] *= inv_sum;
	
}



/*
Computes squared distance metric based on the Battacharyya similarity
coefficient between histograms.

@param h1 first histogram; should be normalized
@param h2 second histogram; should be normalized

@return Returns a squared distance based on the Battacharyya similarity
coefficient between \a h1 and \a h2
*/
float histo_dist_sq(histogram *h1, histogram* h2)
{
	float* hist1, *hist2;
	float sum = 0;
	int i, n;

	if ((!h1) || (!h2))
	{
		return 0.0;
	}
	n = h1->n;
	hist1 = h1->histo;
	hist2 = h2->histo;

	/*
	According the the Battacharyya similarity coefficient,

	D = \sqrt{ 1 - \sum_1^n{ \sqrt{ h_1(i) * h_2(i) } } }
	*/
	for (i = 0; i < n; i++)
		sum += sqrt(hist1[i] * hist2[i]);
	return sum;
}
Mat img2hsv(Mat image, Rect roi)
{
	Mat imgroi = RectTools::subwindow(image, roi, cv::BORDER_REPLICATE);
	Mat tmproi,hsv;
	
	imgroi.copyTo(tmproi);
	tmproi.convertTo(hsv, CV_32FC3, 1.0 / 255.0);
	cvtColor(hsv, hsv, CV_BGR2HSV);
	return hsv;
	
}

