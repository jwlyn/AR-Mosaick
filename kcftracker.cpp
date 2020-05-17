/*

Tracker based on Kernelized Correlation Filter (KCF) [1] and Circulant Structure with Kernels (CSK) [2].
CSK is implemented by using raw gray level features, since it is a single-channel filter.
KCF is implemented by using HOG features (the default), since it extends CSK to multiple channels.

[1] J. F. Henriques, R. Caseiro, P. Martins, J. Batista,
"High-Speed Tracking with Kernelized Correlation Filters", TPAMI 2015.

[2] J. F. Henriques, R. Caseiro, P. Martins, J. Batista,
"Exploiting the Circulant Structure of Tracking-by-detection with Kernels", ECCV 2012.

Authors: Joao Faro, Christian Bailer, Joao F. Henriques
Contacts: joaopfaro@gmail.com, Christian.Bailer@dfki.de, henriques@isr.uc.pt
Institute of Systems and Robotics - University of Coimbra / Department Augmented Vision DFKI


Constructor parameters, all boolean:
    hog: use HOG features (default), otherwise use raw pixels
    fixed_window: fix window size (default), otherwise use ROI size (slower but more accurate)
    multiscale: use multi-scale tracking (default; cannot be used with fixed_window = true)

Default values are set for all properties of the tracker depending on the above choices.
Their values can be customized further before calling init():
    interp_factor: linear interpolation factor for adaptation
    sigma: gaussian kernel bandwidth
    lambda: regularization
    cell_size: HOG cell size
    padding: area surrounding the target, relative to its size
    output_sigma_factor: bandwidth of gaussian target
    template_size: template size in pixels, 0 to use ROI size
    scale_step: scale step for multi-scale estimation, 1 to disable it
    scale_weight: to downweight detection scores of other scales for added stability

For speed, the value (template_size/cell_size) should be a power of 2 or a product of small prime numbers.

Inputs to init():
   image is the initial frame.
   roi is a cv::Rect with the target positions in the initial frame

Inputs to update():
   image is the current frame.

Outputs of update():
   cv::Rect with target positions for the current frame


By downloading, copying, installing or using the software you agree to this license.
If you do not agree to this license, do not download, install,
copy or use the software.


                          License Agreement
               For Open Source Computer Vision Library
                       (3-clause BSD License)

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  * Neither the names of the copyright holders nor the names of the contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.

This software is provided by the copyright holders and contributors "as is" and
any express or implied warranties, including, but not limited to, the implied
warranties of merchantability and fitness for a particular purpose are disclaimed.
In no event shall copyright holders or contributors be liable for any direct,
indirect, incidental, special, exemplary, or consequential damages
(including, but not limited to, procurement of substitute goods or services;
loss of use, data, or profits; or business interruption) however caused
and on any theory of liability, whether in contract, strict liability,
or tort (including negligence or otherwise) arising in any way out of
the use of this software, even if advised of the possibility of such damage.
 */

#ifndef _KCFTRACKER_HEADERS
#include "kcftracker.hpp"
#include "ffttools.hpp"
#include "recttools.hpp"
#include "fhog.hpp"
#include "labdata.hpp"
#endif
#include <iostream>
#include <fstream>
#include <sstream>
//#include <windows.h>


using namespace std;
using namespace cv;


// Constructor
KCFTracker::KCFTracker(bool hog, bool fixed_window, bool multiscale, bool lab)
{
	frame_count = 0;
    // Parameters equal in all cases
    lambda = 0.0001;
    padding = 3.0; 
    //output_sigma_factor = 0.1;
    output_sigma_factor = 0.135;
	_labfeatures = false;

    if (hog) {    // HOG
        // VOT
        interp_factor = 0.012;
        sigma = 0.6; 
        // TPAMI
        //interp_factor = 0.02;
        //sigma = 0.5; 
        cell_size = 4;
        _hogfeatures = true;

        if (lab) {
            interp_factor = 0.005;
            sigma = 0.4; 
            //output_sigma_factor = 0.025;
            output_sigma_factor = 0.1;

            _labfeatures = true;
        //    _labCentroids = cv::Mat(nClusters, 3, CV_32FC1, &data);
            cell_sizeQ = cell_size*cell_size;
        }
        else{
            _labfeatures = false;
        }
    }
    else {   // RAW
        interp_factor = 0.0225;
        sigma = 0.2; 
        cell_size = 1;
        _hogfeatures = false;
		printf("use gray feature.\n");
        if (lab) {
            printf("Lab features are only used with HOG features.\n");
            _labfeatures = false;
        }
    }


    if (multiscale) { // multiscale
        template_size = 104;
        //template_size = 100;
        scale_step = 1.1;
        scale_weight = 1.0;
        if (!fixed_window) {
            //printf("Multiscale does not support non-fixed window.\n");
            fixed_window = true;
        }
    }
    else if (fixed_window) {  // fit correction without multiscale
        template_size = 104;
        //template_size = 100;
        scale_step = 1.1;
		scale_weight = 1.0;
    }
    else {
        template_size = 1;
        scale_step = 1;
    }
}
KCFTracker::~KCFTracker()
{
	
}

// Initialize tracker 
bool KCFTracker::init(const cv::Rect &roi, cv::Mat image)
{
	bool ret = false;
	if((roi.width<16)||(roi.height<16))
	{
	  return false;
	}
    _roi = roi;
    //assert(roi.width >= 0 && roi.height >= 0);

	getTemplateSize(image);
    _tmpl = getFeatures(image, 1);

	tmpl_original = getgray(image,_roi);
		
    _prob = createGaussianPeak(size_patch[0], size_patch[1]);
    _alphaf = cv::Mat(size_patch[0], size_patch[1], CV_32FC2, float(0));

    //_num = cv::Mat(size_patch[0], size_patch[1], CV_32FC2, float(0));
    //_den = cv::Mat(size_patch[0], size_patch[1], CV_32FC2, float(0));
	//cout << "size_patch[0]: "<< size_patch[0] << endl;
	//cout << "size_patch[1]: "<< size_patch[1] << endl;	
    train(_tmpl, 1.0); // train with initial frame

	
	//hsv histogram
#if 0
	IplImage* tmp, *hsv, img;
	img = image;
	cvSetImageROI(&img, roi);
	tmp = cvCreateImage(cvGetSize(&img), IPL_DEPTH_8U, 3);
	cvCopy(&img, tmp, NULL);
	cvResetImageROI(&img);


	hsv = bgr2hsv(tmp);
	/*Mat mhsv = cvarrToMat(hsv);
	
	Mat bgr;
	imgroi.convertTo(bgr, CV_32FC3, 1.0 / 255.0);
	cvtColor(bgr, bgr, CV_BGR2HSV);
	float *aaa = (float*)bgr.data;
	float *bbb = (float*)mhsv.data;
	
	for (int i = 0; i < mtmp.cols;i++)
	for (int j = 0; j < mtmp.rows; j++)
	{
		if (*aaa != *bbb)
			cout << *aaa << "/" << *bbb << endl;
		aaa++;
		bbb++;
	}*/

	//ref_histos = calc_histogram1( hsv);
	Mat roi_temp = cvarrToMat(hsv);
	ref_histos = calc_histogram(roi_temp);
	normalize_histogram(ref_histos);
	cvReleaseImage(&tmp);
#else
	//hsv histogram for judgement
	if(_labfeatures)
	{
		Mat hsv = img2hsv(image, roi);
		ret = calc_histogram(hsv,&ref_histos);
		if(!ret)
			return false;
		normalize_histogram(&ref_histos);
	}
	
#endif
	return true;
 }
// Update position based on the new frame
cv::Rect  KCFTracker::getRect()
{
	return _roi;
}
bool KCFTracker::update(cv::Mat image)
{
	cv::Rect_<float> roi_tmp;
    float scale_temp = _scale;
	
	if(image.empty())
	{
		return false; 
	}
    if (_roi.x + _roi.width <= 0) _roi.x = -_roi.width + 1;
    if (_roi.y + _roi.height <= 0) _roi.y = -_roi.height + 1;
    if (_roi.x >= image.cols - 1) _roi.x = image.cols - 2;
    if (_roi.y >= image.rows - 1) _roi.y = image.rows - 2;
	roi_tmp = _roi;

    float cx = _roi.x + _roi.width / 2.0f;
    float cy = _roi.y + _roi.height / 2.0f;

	double t = (double)getTickCount();	
    //float peak_value;
    cv::Point2f res = detect(_tmpl, getFeatures(image, 1.0f), peak_value, psr_value);
	t = (double)cvGetTickCount() - t;
	//printf("detect time = %gms\n", t / (cvGetTickFrequency() * 1000));
	frame_count++;
	if(frame_count>=2)
	{
		//if (scale_step != 1) {
		{
			// Test at a smaller _scale
			float scale_weight_temp = scale_weight*0.9;
			float new_peak_value; 
			float new_psr_value;
			cv::Point2f new_res = detect(_tmpl, getFeatures(image, 1.0f / scale_step), new_peak_value, psr_value);

			if (scale_weight_temp * new_peak_value > peak_value) {
				res = new_res;
				peak_value = new_peak_value;
				psr_value = new_psr_value;
				scale_temp /= scale_step;
				//_roi.width /= scale_step;
				//_roi.height /= scale_step;
				roi_tmp.width /= scale_step;
				roi_tmp.height /= scale_step;
			}

			
		}
		frame_count=0;
	}
	else if (frame_count==1)
	{
		float new_peak_value;
		//cv::Point2f new_res = detect(_tmpl, getFeatures(image,  1.0f / scale_step), new_peak_value);
		// Test at a bigger _scale
		float new_psr_value;
		cv::Point2f new_res = detect(_tmpl, getFeatures(image, scale_step), new_peak_value, new_psr_value);
	//	cout << "**********" << endl; 
		float scale_weight_temp = scale_weight*0.93;
		if (scale_weight_temp * new_peak_value > peak_value) {
			res = new_res;
			peak_value = new_peak_value;
			psr_value = new_psr_value;
			scale_temp *= scale_step;
			// _roi.width *= scale_step;
			//_roi.height *= scale_step;

			roi_tmp.width *= scale_step;
			roi_tmp.height *= scale_step;
		}
	}
	//if (roi_tmp.width>155)roi_tmp.width = 155;
	//if (roi_tmp.height>155)roi_tmp.height = 155;
//	cout << "roi_tmp.width: " << roi_tmp.width << endl;
    // Adjust by cell size and _scale
	roi_tmp.x = cx - roi_tmp.width / 2.0f + ((float) res.x * cell_size * scale_temp);
	roi_tmp.y = cy - roi_tmp.height / 2.0f + ((float) res.y * cell_size * scale_temp);
	if (roi_tmp.x <= 1)roi_tmp.x = 1;
	if (roi_tmp.y <= 1)roi_tmp.y = 1;
    if (roi_tmp.x >= image.cols - 1) roi_tmp.x = image.cols - 1;
    if (roi_tmp.y >= image.rows - 1) roi_tmp.y = image.rows - 1;
    if (roi_tmp.x + roi_tmp.width <= 0) roi_tmp.x = -roi_tmp.width + 2;
    if (roi_tmp.y + roi_tmp.height <= 0) roi_tmp.y = -roi_tmp.height + 2;
	if (roi_tmp.x + roi_tmp.width >= image.cols - 1) roi_tmp.x = image.cols - roi_tmp.width -1;
	if (roi_tmp.y + roi_tmp.height <= 0) roi_tmp.y = image.rows - roi_tmp.height -1;
//	LARGE_INTEGER nFreq1;
//	LARGE_INTEGER nBeginTime1;
//	LARGE_INTEGER nEndTime1;
	cv::Mat ncc1(1, 1, CV_32F);
//	QueryPerformanceFrequency(&nFreq1);
//	QueryPerformanceCounter(&nBeginTime1);

	cv::Mat tmp = getgray(image, roi_tmp);
	matchTemplate(tmp, tmpl_original, ncc1, CV_TM_CCOEFF_NORMED);//CV_TM_CCORR
	template_sim = (((float*)ncc1.data)[0] + 1)*0.5;
	
	

//	QueryPerformanceCounter(&nEndTime1);
	//double time = (double)(nEndTime1.QuadPart - nBeginTime1.QuadPart) / (double)nFreq1.QuadPart;
//	cout << "template matching TimeEnd-TimeStart:" << time << endl;
	
	//imshow("x",tmp);
	//imshow("T", tmpl_original);
	
	if(_labfeatures)
	{
		Mat hsv = img2hsv(image, roi_tmp);
		calc_histogram(hsv,&histos);
		normalize_histogram(&histos);
		hist_similarity = histo_dist_sq(&ref_histos,&histos);
	}
	else
	{
		hist_similarity = 0.0;
	}
//	cout << "match template result:" << template_sim << ",peak:" << peak_value <<",hist distance:"<<hist_similarity<< endl;
	
	
	if (peak_value<0.35 )//|| psr_value <2.3
	{
		return false;
	}
	else if ((template_sim>0.68 || (hist_similarity >= 0.7 || peak_value >= 0.45))) //&& psr_value >2.5
//	else if((template_sim>=0.40)||(hist_similarity>=0.40))
	{
	    _roi = roi_tmp;
		_scale = scale_temp;
	    assert(_roi.width >= 0 && _roi.height >= 0);
	    
		
		//if (psr_value > 3.5)
		{
			cv::Mat x = getFeatures(image, 1);
			train(x, interp_factor);
		}

	//	t = 1000 * ((double)getTickCount() - t) / getTickFrequency();
	///	cout << "detect time used:" << t << endl;
	}
	else
	{
		return false;
	}
	
	

    return true;
}


// Detect object in the current frame.
cv::Point2f KCFTracker::detect(cv::Mat z, cv::Mat x, float &peak_value, float &psr_value)
{
    using namespace FFTTools;

	//float peak_value;
	
    cv::Mat k = gaussianCorrelation(x, z);
    cv::Mat res = (real(fftd(complexMultiplication(_alphaf, fftd(k)), true)));
	Mat res_n; 
	normalize(res,res_n,255.0,0.0,NORM_MINMAX);

    //minMaxLoc only accepts doubles for the peak, and integer points for the coordinates
    cv::Point2i pi;
    double pv;
    cv::minMaxLoc(res, NULL, &pv, NULL, &pi);
    peak_value = (float) pv;
    //subpixel peak estimation, coordinates will be non-integer
    cv::Point2f p((float)pi.x, (float)pi.y);

	/***********add PSR ********/
	cv::Point2i pi_n;
    double pv_n;
	cv::minMaxLoc(res_n, NULL, &pv_n, NULL, &pi_n);
	Mat PSR_mask = Mat::zeros(res.rows,res.cols, CV_8U);
    Scalar mean,stddev;
	//Define PSR mask
    int win_size = floor((float)PSR_mask.cols/4);
	Rect mini_roi = Rect(std::max(pi_n.x - win_size, 0), std::max(pi_n.y - win_size, 0), win_size * 2, win_size * 2);
	Rect cent_roi = Rect(std::max(pi_n.x - win_size/4, 0), std::max(pi_n.y - win_size/4, 0), win_size/2 , win_size/2 );
	//Handle image boundaries
    if ( (mini_roi.x+mini_roi.width) > PSR_mask.cols )
    {
        mini_roi.width = PSR_mask.cols - mini_roi.x;
    }
    if ( (mini_roi.y+mini_roi.height) > PSR_mask.rows )
    {
        mini_roi.height = PSR_mask.rows - mini_roi.y;
    }
	if ((cent_roi.x + cent_roi.width) > PSR_mask.cols)
	{
		cent_roi.width = PSR_mask.cols - cent_roi.x;
	}
	if ((cent_roi.y + cent_roi.height) > PSR_mask.rows)
	{
		cent_roi.height = PSR_mask.rows - cent_roi.y;
	}
    Mat temp = PSR_mask(mini_roi);
    temp.setTo( 255);
	Mat temp_cent = PSR_mask(cent_roi);
	temp_cent.setTo(0);
	/*for(int i = 0;i< res.rows; i = i +4 )
	for (int j = 0; j< res.cols; j = j + 4)
		{
			if(sqrt((j-pi_n.x)*(j-pi_n.x)+(i-pi_n.y)*(i-pi_n.y)) < res.rows/3 && sqrt((j-pi_n.x)*(j-pi_n.x)+(i-pi_n.y)*(i-pi_n.y)) > res.rows/8)
				PSR_mask.at<uchar>(i,j) = 255;
		}*/
	//imshow("PSR_mask",PSR_mask);
	meanStdDev(res_n, mean, stddev, PSR_mask);   //Compute matrix mean and std
	//cout <<"res_n mean: " << mean <<", stddev: " << stddev<<endl;
	psr_value = (pv_n - mean.val[0]) / stddev.val[0];
	//cout << "PSR: " << psr_value << endl;     //Compute PSR

	/*********end add PSR******/
	//psr_value = 5;
    if (pi.x > 0 && pi.x < res.cols-1) {
        p.x += subPixelPeak(res.at<float>(pi.y, pi.x-1), peak_value, res.at<float>(pi.y, pi.x+1));
    }

    if (pi.y > 0 && pi.y < res.rows-1) {
        p.y += subPixelPeak(res.at<float>(pi.y-1, pi.x), peak_value, res.at<float>(pi.y+1, pi.x));
    }

    p.x -= (res.cols) / 2;
    p.y -= (res.rows) / 2;

    return p;
}

// train tracker with a single image
void KCFTracker::train(cv::Mat x, float train_interp_factor)
{
    using namespace FFTTools;

    cv::Mat k = gaussianCorrelation(x, x);
    cv::Mat alphaf = complexDivision(_prob, (fftd(k) + lambda));
    
    _tmpl = (1 - train_interp_factor) * _tmpl + (train_interp_factor) * x;
    _alphaf = (1 - train_interp_factor) * _alphaf + (train_interp_factor) * alphaf;


    /*cv::Mat kf = fftd(gaussianCorrelation(x, x));
    cv::Mat num = complexMultiplication(kf, _prob);
    cv::Mat den = complexMultiplication(kf, kf + lambda);
    
    _tmpl = (1 - train_interp_factor) * _tmpl + (train_interp_factor) * x;
    _num = (1 - train_interp_factor) * _num + (train_interp_factor) * num;
    _den = (1 - train_interp_factor) * _den + (train_interp_factor) * den;

    _alphaf = complexDivision(_num, _den);*/

}

// Evaluates a Gaussian kernel with bandwidth SIGMA for all relative shifts between input images X and Y, which must both be MxN. They must    also be periodic (ie., pre-processed with a cosine window).
cv::Mat KCFTracker::gaussianCorrelation(cv::Mat x1, cv::Mat x2)
{
    using namespace FFTTools;
    cv::Mat c = cv::Mat( cv::Size(size_patch[1], size_patch[0]), CV_32F, cv::Scalar(0) );
    // HOG features
    if (_hogfeatures) {
        cv::Mat caux;
        cv::Mat x1aux;
        cv::Mat x2aux;
        for (int i = 0; i < size_patch[2]; i++) {
            x1aux = x1.row(i);   // Procedure do deal with cv::Mat multichannel bug
            x1aux = x1aux.reshape(1, size_patch[0]);
            x2aux = x2.row(i).reshape(1, size_patch[0]);
            cv::mulSpectrums(fftd(x1aux), fftd(x2aux), caux, 0, true); 
            caux = fftd(caux, true);
            rearrange(caux);
            caux.convertTo(caux,CV_32F);
            c = c + real(caux);
        }
    }
    // Gray features
    else {
        cv::mulSpectrums(fftd(x1), fftd(x2), c, 0, true);
        c = fftd(c, true);
        rearrange(c);
        c = real(c);
    }
    cv::Mat d; 
    cv::max(( (cv::sum(x1.mul(x1))[0] + cv::sum(x2.mul(x2))[0])- 2. * c) / (size_patch[0]*size_patch[1]*size_patch[2]) , 0, d);


    cv::Mat k;
    cv::exp((-d / (sigma * sigma)), k);
    return k;
}

// Create Gaussian Peak. Function called only in the first frame.
cv::Mat KCFTracker::createGaussianPeak(int sizey, int sizex)
{
	cv::Mat_<float> res(sizey, sizex);

	int syh = (sizey) / 2;
	int sxh = (sizex) / 2;

	float output_sigma = std::sqrt((float)sizex * sizey) / padding * output_sigma_factor;
	float mult = -0.5 / (output_sigma * output_sigma);

	for (int i = 0; i < sizey; i++)
	for (int j = 0; j < sizex; j++)
	{
		int ih = i - syh;
		int jh = j - sxh;
		res(i, j) = std::exp(mult * (float)(ih * ih + jh * jh));
	}
	return FFTTools::fftd(res);
}
// Obtain sub-window from image
cv::Mat KCFTracker::getgray(const cv::Mat & image,cv::Rect_<float> roi)
{

    cv::Mat FeaturesMap;  
    cv::Mat z = RectTools::subwindow(image, roi, cv::BORDER_REPLICATE);
    
    if (z.cols != _tmpl_sz.width || z.rows != _tmpl_sz.height) {
        cv::resize(z, z, _tmpl_sz);
    }   

    FeaturesMap = RectTools::getGrayImage(z);
    //FeaturesMap -= (float) 0.5; // In Paper;
    
    //FeaturesMap = hann.mul(FeaturesMap);
    return FeaturesMap;
}

// Obtain sub-window from image, with replication-padding and extract features
cv::Mat KCFTracker::getFeatures(const cv::Mat & image, float scale_adjust)
 {


    cv::Rect extracted_roi;

    float cx = _roi.x + _roi.width / 2;
    float cy = _roi.y + _roi.height / 2;
	//cout << "scale_adjust:" << scale_adjust << " scale:" << _scale << "_tmpl_sz.width:" << _tmpl_sz.width << "_tmpl_sz.height:" << _tmpl_sz.height << endl;
    extracted_roi.width = scale_adjust * _scale * _tmpl_sz.width;
    extracted_roi.height = scale_adjust * _scale * _tmpl_sz.height;
	if (extracted_roi.width > 2100)extracted_roi.width = 2100;
	if (extracted_roi.height > 2100)extracted_roi.height = 2100;
//	cout << "extracted_roi:" << extracted_roi.width << endl;
    // center roi with new size
    extracted_roi.x = cx - extracted_roi.width / 2;
    extracted_roi.y = cy - extracted_roi.height / 2;
//	if (extracted_roi.x <=1)extracted_roi.x = 1;
//	if (extracted_roi.y <= 1)extracted_roi.y = 1;
//	if (extracted_roi.x + extracted_roi.width >= 1920 - 1)extracted_roi.x = 1920 - extracted_roi.width - 1;
//	if (extracted_roi.y + extracted_roi.height >= 1080 - 1)extracted_roi.y = 1080 - extracted_roi.height - 1;
//	cout << "image width:" << image.cols << "image height:" << image.rows << endl;
//  double t = (double)getTickCount();
	cv::Mat FeaturesMap;  
    cv::Mat z = RectTools::subwindow(image, extracted_roi, cv::BORDER_REPLICATE);
//	t = (double)cvGetTickCount() - t;
//	printf("FeaturesMap time = %gms\n", t / (cvGetTickFrequency() * 1000));
    if (z.cols != _tmpl_sz.width || z.rows != _tmpl_sz.height) {
        cv::resize(z, z, _tmpl_sz);
    }   
	
    // HOG features
    if (_hogfeatures) {
        IplImage z_ipl = z;
        CvLSVMFeatureMapCaskade *map;
        getFeatureMaps(&z_ipl, cell_size, &map);
        normalizeAndTruncate(map,0.2f);
        PCAFeatureMaps(map);
        //size_patch[0] = map->sizeY;
        //size_patch[1] = map->sizeX;
        //size_patch[2] = map->numFeatures;

        FeaturesMap = cv::Mat(cv::Size(map->numFeatures,map->sizeX*map->sizeY), CV_32F, map->map);  // Procedure do deal with cv::Mat multichannel bug
        FeaturesMap = FeaturesMap.t();
        freeFeatureMapObject(&map);

        // Lab features
        if (_labfeatures) {
            cv::Mat imgLab;
            cvtColor(z, imgLab, CV_BGR2Lab);
            unsigned char *input = (unsigned char*)(imgLab.data);

            // Sparse output vector
            cv::Mat outputLab = cv::Mat(_labCentroids.rows, size_patch[0]*size_patch[1], CV_32F, float(0));

            int cntCell = 0;
            // Iterate through each cell
            for (int cY = cell_size; cY < z.rows-cell_size; cY+=cell_size){
                for (int cX = cell_size; cX < z.cols-cell_size; cX+=cell_size){
                    // Iterate through each pixel of cell (cX,cY)
                    for(int y = cY; y < cY+cell_size; ++y){
                        for(int x = cX; x < cX+cell_size; ++x){
                            // Lab components for each pixel
                            float l = (float)input[(z.cols * y + x) * 3];
                            float a = (float)input[(z.cols * y + x) * 3 + 1];
                            float b = (float)input[(z.cols * y + x) * 3 + 2];

                            // Iterate trough each centroid
                            float minDist = FLT_MAX;
                            int minIdx = 0;
                            float *inputCentroid = (float*)(_labCentroids.data);
                            for(int k = 0; k < _labCentroids.rows; ++k){
                                float dist = ( (l - inputCentroid[3*k]) * (l - inputCentroid[3*k]) )
                                           + ( (a - inputCentroid[3*k+1]) * (a - inputCentroid[3*k+1]) ) 
                                           + ( (b - inputCentroid[3*k+2]) * (b - inputCentroid[3*k+2]) );
                                if(dist < minDist){
                                    minDist = dist;
                                    minIdx = k;
                                }
                            }
                            // Store result at output
                            outputLab.at<float>(minIdx, cntCell) += 1.0 / cell_sizeQ; 
                            //((float*) outputLab.data)[minIdx * (size_patch[0]*size_patch[1]) + cntCell] += 1.0 / cell_sizeQ; 
                        }
                    }
                    cntCell++;
                }
            }
            // Update size_patch[2] and add features to FeaturesMap
            //size_patch[2] += _labCentroids.rows;
            FeaturesMap.push_back(outputLab);
        }
    }
    else {
        FeaturesMap = RectTools::getGrayImage(z);
        FeaturesMap -= (float) 0.5; // In Paper;
        //size_patch[0] = z.rows;
        //size_patch[1] = z.cols;
        //size_patch[2] = 1;  
    }
  //  cout << "FeaturesMap rows: "<<FeaturesMap.rows << " FeaturesMap cols: "<<FeaturesMap.cols<<endl;
    FeaturesMap = hann.mul(FeaturesMap);
	imshow("FeaturesMap",FeaturesMap);
    return FeaturesMap;
}

// Obtain sub-window from image, with replication-padding and extract features
void KCFTracker::getTemplateSize(const cv::Mat & image)
 {
    cv::Rect extracted_roi;

    float cx = _roi.x + _roi.width / 2;
    float cy = _roi.y + _roi.height / 2;

    int padded_w = _roi.width * padding;
    int padded_h = _roi.height * padding;
    
    if (template_size > 1) {  // Fit largest dimension to the given template size
        if (padded_w >= padded_h)  //fit to width
            _scale = padded_w / (float) template_size;
        else
            _scale = padded_h / (float) template_size;
		//cout << "ori padded_w:" << padded_w << endl;
		//cout << "ori padded_h:" << padded_h << endl;
		//cout << "ori template_size:" << template_size << endl;
		//cout << "ori scale:" << _scale  << endl;
        _tmpl_sz.width = padded_w / _scale;
        _tmpl_sz.height = padded_h / _scale;
		//cout << "_tmpl_sz width:" << _tmpl_sz.width << endl;
		//cout << "_tmpl_sz height:" << _tmpl_sz.height << endl;
    }
    else {  //No template size given, use ROI size
        _tmpl_sz.width = padded_w;
        _tmpl_sz.height = padded_h;
        _scale = 1;
        // original code from paper:
        /*if (sqrt(padded_w * padded_h) >= 100) {   //Normal size
            _tmpl_sz.width = padded_w;
            _tmpl_sz.height = padded_h;
            _scale = 1;
        }
        else {   //ROI is too big, track at half size
            _tmpl_sz.width = padded_w / 2;
            _tmpl_sz.height = padded_h / 2;
            _scale = 2;
        }*/
    }

    if (_hogfeatures) {
        // Round to cell size and also make it even
        _tmpl_sz.width = ( ( (int)(_tmpl_sz.width / (2 * cell_size)) ) * 2 * cell_size ) + cell_size*2;
        _tmpl_sz.height = ( ( (int)(_tmpl_sz.height / (2 * cell_size)) ) * 2 * cell_size ) + cell_size*2;
    }
    else {  //Make number of pixels even (helps with some logic involving half-dimensions)
        _tmpl_sz.width = (_tmpl_sz.width / 2) * 2;
        _tmpl_sz.height = (_tmpl_sz.height / 2) * 2;
    }

   
    // HOG features
    if (_hogfeatures)
	{      
        CvLSVMFeatureMapCaskade *map;
		getFeatureSize(_tmpl_sz.width,_tmpl_sz.height, cell_size, &map);
	    size_patch[0] = map->sizeY;
	    size_patch[1] = map->sizeX;
	    size_patch[2] = map->numFeatures;
        freeFeatureMapObject(&map);

        // Lab features
        if (_labfeatures) 
		{
            // Update size_patch[2] and add features to FeaturesMap
            size_patch[2] += _labCentroids.rows;
        }
    }
    else {
        size_patch[0] = _tmpl_sz.height;
        size_patch[1] = _tmpl_sz.width;
        size_patch[2] = 1;  
    }
    
    createHanningMats();
}
#if 0
// Obtain sub-window from image, with replication-padding and extract features
cv::Mat KCFTracker::getFeatures(const cv::Mat & image, bool inithann, float scale_adjust)
{
    cv::Rect extracted_roi;

    float cx = _roi.x + _roi.width / 2;
    float cy = _roi.y + _roi.height / 2;

    if (inithann) {
        int padded_w = _roi.width * padding;
        int padded_h = _roi.height * padding;
        
        if (template_size > 1) {  // Fit largest dimension to the given template size
            if (padded_w >= padded_h)  //fit to width
                _scale = padded_w / (float) template_size;
            else
                _scale = padded_h / (float) template_size;

            _tmpl_sz.width = padded_w / _scale;
            _tmpl_sz.height = padded_h / _scale;
        }
        else {  //No template size given, use ROI size
            _tmpl_sz.width = padded_w;
            _tmpl_sz.height = padded_h;
            _scale = 1;
            // original code from paper:
            /*if (sqrt(padded_w * padded_h) >= 100) {   //Normal size
                _tmpl_sz.width = padded_w;
                _tmpl_sz.height = padded_h;
                _scale = 1;
            }
            else {   //ROI is too big, track at half size
                _tmpl_sz.width = padded_w / 2;
                _tmpl_sz.height = padded_h / 2;
                _scale = 2;
            }*/
        }

        if (_hogfeatures) {
            // Round to cell size and also make it even
            _tmpl_sz.width = ( ( (int)(_tmpl_sz.width / (2 * cell_size)) ) * 2 * cell_size ) + cell_size*2;
            _tmpl_sz.height = ( ( (int)(_tmpl_sz.height / (2 * cell_size)) ) * 2 * cell_size ) + cell_size*2;
        }
        else {  //Make number of pixels even (helps with some logic involving half-dimensions)
            _tmpl_sz.width = (_tmpl_sz.width / 2) * 2;
            _tmpl_sz.height = (_tmpl_sz.height / 2) * 2;
        }
    }

    extracted_roi.width = scale_adjust * _scale * _tmpl_sz.width;
    extracted_roi.height = scale_adjust * _scale * _tmpl_sz.height;

    // center roi with new size
    extracted_roi.x = cx - extracted_roi.width / 2;
    extracted_roi.y = cy - extracted_roi.height / 2;

    cv::Mat FeaturesMap;  
    cv::Mat z = RectTools::subwindow(image, extracted_roi, cv::BORDER_REPLICATE);
    
    if (z.cols != _tmpl_sz.width || z.rows != _tmpl_sz.height) {
        cv::resize(z, z, _tmpl_sz);
    }   

    // HOG features
    if (_hogfeatures) {
        IplImage z_ipl = z;
        CvLSVMFeatureMapCaskade *map;
        getFeatureMaps(&z_ipl, cell_size, &map);
        normalizeAndTruncate(map,0.2f);
        PCAFeatureMaps(map);
        size_patch[0] = map->sizeY;
        size_patch[1] = map->sizeX;
        size_patch[2] = map->numFeatures;

        FeaturesMap = cv::Mat(cv::Size(map->numFeatures,map->sizeX*map->sizeY), CV_32F, map->map);  // Procedure do deal with cv::Mat multichannel bug
        FeaturesMap = FeaturesMap.t();
        freeFeatureMapObject(&map);

        // Lab features
        if (_labfeatures) {
            cv::Mat imgLab;
            cvtColor(z, imgLab, CV_BGR2Lab);
            unsigned char *input = (unsigned char*)(imgLab.data);

            // Sparse output vector
            cv::Mat outputLab = cv::Mat(_labCentroids.rows, size_patch[0]*size_patch[1], CV_32F, float(0));

            int cntCell = 0;
            // Iterate through each cell
            for (int cY = cell_size; cY < z.rows-cell_size; cY+=cell_size){
                for (int cX = cell_size; cX < z.cols-cell_size; cX+=cell_size){
                    // Iterate through each pixel of cell (cX,cY)
                    for(int y = cY; y < cY+cell_size; ++y){
                        for(int x = cX; x < cX+cell_size; ++x){
                            // Lab components for each pixel
                            float l = (float)input[(z.cols * y + x) * 3];
                            float a = (float)input[(z.cols * y + x) * 3 + 1];
                            float b = (float)input[(z.cols * y + x) * 3 + 2];

                            // Iterate trough each centroid
                            float minDist = FLT_MAX;
                            int minIdx = 0;
                            float *inputCentroid = (float*)(_labCentroids.data);
                            for(int k = 0; k < _labCentroids.rows; ++k){
                                float dist = ( (l - inputCentroid[3*k]) * (l - inputCentroid[3*k]) )
                                           + ( (a - inputCentroid[3*k+1]) * (a - inputCentroid[3*k+1]) ) 
                                           + ( (b - inputCentroid[3*k+2]) * (b - inputCentroid[3*k+2]) );
                                if(dist < minDist){
                                    minDist = dist;
                                    minIdx = k;
                                }
                            }
                            // Store result at output
                            outputLab.at<float>(minIdx, cntCell) += 1.0 / cell_sizeQ; 
                            //((float*) outputLab.data)[minIdx * (size_patch[0]*size_patch[1]) + cntCell] += 1.0 / cell_sizeQ; 
                        }
                    }
                    cntCell++;
                }
            }
            // Update size_patch[2] and add features to FeaturesMap
            size_patch[2] += _labCentroids.rows;
            FeaturesMap.push_back(outputLab);
        }
    }
    else {
        FeaturesMap = RectTools::getGrayImage(z);
        FeaturesMap -= (float) 0.5; // In Paper;
        size_patch[0] = z.rows;
        size_patch[1] = z.cols;
        size_patch[2] = 1;  
    }
    
    if (inithann) {
        createHanningMats();
    }
    FeaturesMap = hann.mul(FeaturesMap);
    return FeaturesMap;
}
#endif 
// Initialize Hanning window. Function called only in the first frame.
void KCFTracker::createHanningMats()
{   
    cv::Mat hann1t = cv::Mat(cv::Size(size_patch[1],1), CV_32F, cv::Scalar(0));
    cv::Mat hann2t = cv::Mat(cv::Size(1,size_patch[0]), CV_32F, cv::Scalar(0)); 

    for (int i = 0; i < hann1t.cols; i++)
        hann1t.at<float > (0, i) = 0.5 * (1 - std::cos(2 * 3.14159265358979323846 * i / (hann1t.cols - 1)));
    for (int i = 0; i < hann2t.rows; i++)
        hann2t.at<float > (i, 0) = 0.5 * (1 - std::cos(2 * 3.14159265358979323846 * i / (hann2t.rows - 1)));

    cv::Mat hann2d = hann2t * hann1t;
    // HOG features
    if (_hogfeatures) {
        cv::Mat hann1d = hann2d.reshape(1,1); // Procedure do deal with cv::Mat multichannel bug
        hann = cv::Mat(cv::Size(size_patch[0]*size_patch[1], size_patch[2]), CV_32F, cv::Scalar(0));
        for (int i = 0; i < size_patch[2]; i++) {
            for (int j = 0; j<size_patch[0]*size_patch[1]; j++) {
                hann.at<float>(i,j) = hann1d.at<float>(0,j);
            }
        }
    }
    // Gray features
    else {
        hann = hann2d;
    }
}

// Calculate sub-pixel peak for one dimension
float KCFTracker::subPixelPeak(float left, float center, float right)
{   
    float divisor = 2 * center - right - left;

    if (divisor == 0)
        return 0;
    
    return 0.5 * (right - left) / divisor;
}
