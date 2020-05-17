#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <windows.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include "kcftracker.hpp"

using namespace std;
using namespace cv;


//Find the intersection of two sets of line segments, where p1 and p3 form a line segment, p2 and p4 form a line segment
Point Intersection(Point p1, Point p2, Point p3, Point p4)
{
	int xm = p2.x*p1.y - p4.x*p1.y - p1.x*p2.y + p3.x*p2.y
		- p2.x*p3.y + p4.x*p3.y + p1.x*p4.y - p3.x*p4.y;
	int ym = p2.y*p1.x - p4.y*p1.x - p1.y*p2.x + p3.y*p2.x
		- p2.y*p3.x + p4.y*p3.x + p1.y*p4.x - p3.y*p4.x;

	Point r;
	if (xm == 0 || ym == 0)
	{
		r.x = 0;
		r.y = 0;
		return r;
	}
	int xz = p2.x*p3.x*p1.y - p3.x*p4.x*p1.y - p1.x*p4.x*p2.y + p3.x*p4.x*p2.y
		- p1.x*p2.x*p3.y + p1.x*p4.x*p3.y + p1.x*p2.x*p4.y - p2.x*p3.x*p4.y;
	int yz = p2.y*p3.y*p1.x - p3.y*p4.y*p1.x - p1.y*p4.y*p2.x + p3.y*p4.y*p2.x
		- p1.y*p2.y*p3.x + p1.y*p4.y*p3.x + p1.y*p2.y*p4.x - p2.y*p3.y*p4.x;

	r.x = xz / xm;
	r.y = yz / ym;
	return r;
}

//Distance from point to line
float point2Line(cv::Point2f p1,cv::Point2f lp1,cv::Point2f lp2)
{
	float a,b,c,dis;

	a=lp2.y-lp1.y;
	b=lp1.x-lp2.x;
	c=lp2.x*lp1.y-lp1.x*lp2.y;

	dis=abs(a*p1.x+b*p1.y+c)/sqrt(a*a+b*b);
	return dis;
}

//Define the tracker
struct mulTrackers
{
	KCFTracker tracker;
	Rect initRect;
	Rect resultRect;
	bool isTracking;
};

int main(int argc, char* argv[]){

	bool HOG = false;
	bool FIXEDWINDOW = false;
	bool MULTISCALE = true;
	bool SILENT = true;
	bool LAB = false;

	// Create KCFTracker object
	//KCFTracker tracker(HOG, FIXEDWINDOW, MULTISCALE, LAB);
	vector<mulTrackers> mulTracker;//Used to store multiple KCF trackers
	mulTracker.clear();
	// Frame readed
	Mat frame_rgb,frame;

	// Tracker results
	Rect result;

	// Frame counter
	VideoCapture capture("/IMG_0238.mp4");  //Route

	cvNamedWindow("Image", CV_WINDOW_NORMAL);
	cvSetMouseCallback("Image", mouseHandler, NULL);

	VideoWriter writer("bikecanny.avi", -1, 10, Size(1920, 1080));
	int mouse_event_cnt = 0;
	int frame_cnt = 0;
	while (1)
	{

		capture >> frame_rgb;
		cvtColor(frame_rgb,frame,CV_BGR2GRAY);
		
		for (int i = 0; i < mulTracker.size(); i++)//Track each tracked object. Used to track the four vertices of the bottom surface of the AR Ling cone
		{
			bool tracked = mulTracker[i].tracker.update(frame);
			mulTracker[i].isTracking = tracked;
			mulTracker[i].resultRect = mulTracker[i].tracker.getRect();
			if (tracked && mulTracker.size() == 4)
			{
				cv::circle(frame_rgb,Point(mulTracker[i].resultRect.x + RECT_W/2,mulTracker[i].resultRect.y + RECT_W/2),8,CV_RGB(0,255,0),2);
			}
			else
			{
			}
		}
		
		if(mulTracker.size() == 4 && mouse_event_cnt == 4)//The edges and vertices of the cone are superimposed on the image, of which the bottom 4 uses multi-target tracking, real-time tracking; the vertices are obtained by calculation.
		{
			Point p0 = Point(mulTracker[0].resultRect.x + RECT_W/2,mulTracker[0].resultRect.y + RECT_W/2);//Vertex coordinates of the bottom surface of the Ling cone
			Point p1 = Point(mulTracker[1].resultRect.x + RECT_W/2,mulTracker[1].resultRect.y + RECT_W/2);
			Point p2 = Point(mulTracker[2].resultRect.x + RECT_W/2,mulTracker[2].resultRect.y + RECT_W/2);
			Point p3 = Point(mulTracker[3].resultRect.x + RECT_W/2,mulTracker[3].resultRect.y + RECT_W/2);
			line(frame_rgb, p0, p1, Scalar(0, 0, 255), 2, 8);//Draw a rectangle on the bottom
			line(frame_rgb, p1, p2, Scalar(0, 0, 255), 2, 8);
			line(frame_rgb, p2, p3, Scalar(0, 0, 255), 2, 8);
			line(frame_rgb, p3, p0, Scalar(0, 0, 255), 2, 8);

			Point insec = Intersection(p0,p1,p2,p3);//Midpoint of bottom coordinate
			//cv::circle(frame_rgb,insec,8,CV_RGB(0,0,255),2);
			float Rh = 0.0;
			float Rv = 0.0;
			float l_right = sqrt((float)(p1.x - p2.x)*(p1.x - p2.x) + (p1.y - p2.y) * (p1.y - p2.y));
			float l_left = sqrt((float)(p3.x - p0.x) * (p3.x - p0.x) + (p3.y - p0.y) * (p3.y - p0.y));
			float l_up = sqrt((float)(p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y));
			float l_down = sqrt((float)(p2.x - p3.x) * (p2.x - p3.x) + (p2.y - p3.y) * (p2.y - p3.y));
			float tmp = point2Line(p0,p1,p2) / max(l_left, l_right);
			cout <<tmp<<endl;
			if(tmp >= 1.0) tmp = 1.0;
			Rh = acos(tmp) * 180/3.14159;
			cout << "Rh: " << Rh<<endl;
			Point top ;//
			top.y = insec.y;
			top.x = insec.x;
			if(l_right > l_left)top.x -= l_right/2.0 * sin(Rh * 3.14159/ 180.0)*0.8;
			if(l_right < l_left)top.x += l_left/2.0 * sin(Rh * 3.14159/ 180.0)*0.8;

			
			line(frame_rgb, p0, top, Scalar(0, 0, 255), 2, 8);//Draw a line segment from bottom vertex to vertex
			line(frame_rgb, p1, top, Scalar(0, 0, 255), 2, 8);
			line(frame_rgb, p2, top, Scalar(0, 0, 255), 2, 8);
			line(frame_rgb, p3, top, Scalar(0, 0, 255), 2, 8);
			cv::circle(frame_rgb,top,8,CV_RGB(0,255,255),2);
		}		
		//Eliminate failed tracking targets
		vector<mulTrackers>::iterator itc;
		itc = mulTracker.begin();
		while (itc != mulTracker.end())
		{
			if (itc->isTracking == false)
				itc = mulTracker.erase(itc);
			else
				++itc;
		}

		if (frame_cnt == 128)//In a specific frame, select 4 points as the four vertices of the bottom surface of the AR Ling cone, as subsequent tracking targets
		{
			mulTrackers multiTracker_tmp;
			KCFTracker tracker_tmp(HOG, FIXEDWINDOW, MULTISCALE, LAB);
			multiTracker_tmp.tracker = tracker_tmp;
			multiTracker_tmp.initRect = Rect(94-RECT_W/2, 101-RECT_W/2, RECT_W, RECT_W);//

			mulTracker.push_back(multiTracker_tmp);			
			int m = mulTracker.size();
			mulTracker[m - 1].tracker.init(mulTracker[m - 1].initRect, frame);
		

			//gotBB = false;
			mouse_event_cnt++;
			
			mulTrackers multiTracker_tmp1;
			KCFTracker tracker_tmp1(HOG, FIXEDWINDOW, MULTISCALE, LAB);
			multiTracker_tmp1.tracker = tracker_tmp1;
			multiTracker_tmp1.initRect = Rect(271-RECT_W/2, 126-RECT_W/2, RECT_W, RECT_W);

			mulTracker.push_back(multiTracker_tmp1);			
			m = mulTracker.size();
			mulTracker[m - 1].tracker.init(mulTracker[m - 1].initRect, frame);

			mouse_event_cnt++;
			
			mulTrackers multiTracker_tmp2;
			KCFTracker tracker_tmp2(HOG, FIXEDWINDOW, MULTISCALE, LAB);
			multiTracker_tmp2.tracker = tracker_tmp2;
			multiTracker_tmp2.initRect = Rect(272-RECT_W/2, 290-RECT_W/2, RECT_W, RECT_W);

			mulTracker.push_back(multiTracker_tmp2);			
			m = mulTracker.size();
			mulTracker[m - 1].tracker.init(mulTracker[m - 1].initRect, frame);
			
			mouse_event_cnt++;
			
			mulTrackers multiTracker_tmp3;
			KCFTracker tracker_tmp3(HOG, FIXEDWINDOW, MULTISCALE, LAB);
			multiTracker_tmp3.tracker = tracker_tmp3;
			multiTracker_tmp3.initRect = Rect(94-RECT_W/2, 277-RECT_W/2, RECT_W, RECT_W);

			mulTracker.push_back(multiTracker_tmp3);			
			m = mulTracker.size();
			mulTracker[m - 1].tracker.init(mulTracker[m - 1].initRect, frame);

			mouse_event_cnt++;
		}


		frame_cnt++;
		cout <<frame_cnt<<endl;
		if(frame_cnt>= 471)break;
		imshow("Image", frame_rgb);
		writer << frame_rgb;
	}

	return 0;

}

