#include <math.h>
#include <stdio.h>

#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>

#define HISTORY 30
#define STABILIZE 10
#define THRESH_BINS 100 //for finding thresholds

#define XDIM 640.0
#define YDIM 480.0

//Some thresholds to mask out any remaining jitter, and strong waves. Don't know how to calculate them.
float LOWER =  0.2;
float MID  = .5;
float UPPER = 100.0;

#define rescale(x) x = (x - LOWER)/(UPPER - LOWER)

using namespace cv;

typedef cv::Point3_<char> Pixelc;
typedef cv::Point_<float> Pixel2;
typedef cv::Point3_<float> Pixel3;

void wheel();




int main(int argc, char** argv )
{
	
	
	//wheel();
	
	
	if(argc <2){printf("No video specified\n");wheel();}
	
	
	ocl::setUseOpenCL(true);
	
	char cc[] = "h264";
	
	VideoCapture video = VideoCapture(argv[1]);
	
	cv::VideoWriter video_out("video_out.mp4",
							  video.get(CV_CAP_PROP_FOURCC),
							  video.get(CV_CAP_PROP_FPS)/3,
							  cv::Size(XDIM, YDIM));
	
	if (!video_out.isOpened())
	{
		std::cout << "!!! Output video could not be opened" << std::endl;
		exit(-1);
	}

	
	/*
	
	cv::VideoWriter videoflow("outputflow.mp4",
							   video.get(CV_CAP_PROP_FOURCC),
							   video.get(CV_CAP_PROP_FPS),
							   cv::Size(XDIM, YDIM));
	
	if (!videoflow.isOpened())
	{
		std::cout << "!!! Output video could not be opened" << std::endl;
		exit(-1);
	}
	
	cv::VideoWriter videoagg("outputagg.mp4",
							  video.get(CV_CAP_PROP_FOURCC),
							  video.get(CV_CAP_PROP_FPS),
							  cv::Size(XDIM, YDIM));
	
	if (!videoagg.isOpened())
	{
		std::cout << "!!! Output video could not be opened" << std::endl;
		exit(-1);
	}
	
	cv::VideoWriter videoclass("outputclass.mp4",
							  video.get(CV_CAP_PROP_FOURCC),
							  video.get(CV_CAP_PROP_FPS),
							  cv::Size(XDIM, YDIM));
	
	if (!videoclass.isOpened())
	{
		std::cout << "!!! Output video could not be opened" << std::endl;
		exit(-1);
	}
	*/
	
	int c, r;
	c = (int) video.get(CAP_PROP_FRAME_WIDTH);
	r = (int) video.get(CAP_PROP_FRAME_HEIGHT);
	
	//printf("%f\n",video.get(CV_CAP_PROP_FOURCC));
	//printf("%d\n",VideoWriter::fourcc(cc[0],cc[1],cc[2],cc[3]));
	//exit(0);
	
	float scalex = XDIM/c;
	float scaley = YDIM/r;
	
	//printf("Dimensions: %d by %d, resized to %3.0f by %3.0f\n",r,c,r*scaley,c*scalex);
	
	
	Mat save;
	Mat frame,f2;
	Mat subframe[HISTORY];
	Mat resized;
	Mat flow_raw;
	
	Mat flow;
	Mat stable[STABILIZE];
	
	
	
	UMat u_flow;
	UMat u_f1,u_f2;
	
	
	
	Mat accumulator = Mat::zeros(YDIM, XDIM, CV_32FC3);
	for(int j = 0 ; j< STABILIZE; j++){stable[j] = Mat::zeros(YDIM, XDIM, CV_32FC2);}
	
	Mat ones = Mat::ones(YDIM, XDIM, CV_32FC3);
	Mat out = Mat::zeros(YDIM, XDIM, CV_32FC3);
	
	Mat splitarr[2];
	namedWindow("Rip Current Detector", WINDOW_AUTOSIZE );
	namedWindow("Flow", WINDOW_AUTOSIZE );
	//namedWindow("Classifier", WINDOW_AUTOSIZE );
	//namedWindow("Accumulator", WINDOW_AUTOSIZE );
	
	
	
	int i;
	
	
	int turn = 0;
	video.read(frame);
	
	if(frame.empty()){exit(1);}
	resize(frame,subframe[0],Size(),scalex,scaley,INTER_AREA);
	cvtColor(subframe[0],f2,COLOR_BGR2GRAY);
	f2.copyTo(u_f1);
	
	int hist[THRESH_BINS] = {0};
	int histsum = 0;
	
	for( i = 1; true; i++){//fix video read, right now it's 1/4 realtime.

		
		
		video.read(frame);
		video.read(frame); //skip frames for speed
		video.read(frame);
		
		if(frame.empty()){break;}
		
		resize(frame,subframe[i%HISTORY],Size(),scalex,scaley,INTER_AREA);
		cvtColor(subframe[i%HISTORY],f2,COLOR_BGR2GRAY);
		if(turn){
			cvtColor(subframe[i%HISTORY],f2,COLOR_BGR2GRAY);
			f2.copyTo(u_f1);
			calcOpticalFlowFarneback(u_f2,u_f1, u_flow, 0.5, 3, 5, 3, 15, 1.2, 0);
			//printf("tick\n");
		}else{
			f2.copyTo(u_f2);
			calcOpticalFlowFarneback(u_f1,u_f2, u_flow, 0.5, 3, 5, 3, 15, 1.2, 0);
			//printf("tock\n");
		}
		turn = !turn;
		
		flow_raw = u_flow.getMat(ACCESS_READ);
		
		for(int j = 0; j<STABILIZE; j++){
			add(flow_raw,stable[j],stable[j]);
		}
	
		//Scalar fmean,fstddev;
		//meanStdDev(flow_raw,fmean,fstddev,noArray());
		
		
		Mat current = stable[i%STABILIZE]*(1.0/STABILIZE);
		
		current.forEach<Pixel2>([&](Pixel2& pixel, const int position[]) -> void {
			
			float tx = pixel.x;
			float ty = -pixel.y;
			
			
			int bin = (int) floor(atan(ty/tx)/M_PI  *18  );//Begins to calculate the angle as an integer between 1 and 35.
			
			
			
			if(ty>0){
				if(tx>0){
					bin = bin;
				}else{
					bin = 18 + bin;
				}
			}else{
				
				if(tx<0){
					bin = bin+18;
				}else{
					bin = 36 + bin;
				}
			}
			pixel.x = (float)bin*10;
			
			
			
			pixel.y = sqrt(tx*tx + ty*ty);
			
		});
		
		
		
		
		int resolution = 10;
		
		for (int y = 0; y < YDIM; y++) {
			Pixel2* ptr = current.ptr<Pixel2>(y, 0);
			const Pixel2* ptr_end = ptr + (int)XDIM;
			for (int x = 0 ; ptr != ptr_end; ++ptr, x++) {
				int bin = (ptr->y) * resolution;
				if(bin < THRESH_BINS &&  bin >= 0  /*&& direction >0 && direction<180*/) {hist[bin]++; histsum++;}
				
			}
		}
		
		int threshsum = 0;
		int bin = THRESH_BINS-1;
		while(threshsum < (histsum*.03)){
			threshsum += hist[bin];
			bin--;
		}
		UPPER = bin/float(resolution);
		//printf("%f\n",UPPER);

		

		Mat accumulator2 = Mat::zeros(YDIM, XDIM, CV_32FC3);
		Mat waterclass = Mat::zeros(YDIM, XDIM, CV_32FC3);
		

		
		current.forEach<Pixel2>([&](Pixel2& pixel, const int position[]) -> void {
			
			Pixel3* classptr = waterclass.ptr<Pixel3>(position[0],position[1]);
			//Pixel3* origptr = subframe[i%HISTORY].ptr<Pixel3>(position[0],position[1]);
			Pixel3 * pt = accumulator2.ptr<Pixel3>(position[0],position[1]);
			float dir = pixel.x;
			float val = pixel.y ;
			if(val > UPPER){/*classptr->x = .5;*/ pt->x++;}/*else{
				if(val > MID){classptr->z = 1;}else{
					if(val > LOWER){classptr->z = .5;}else{
						 {classptr->y = .5;}
					}
				}
			}*/
			
			if(val < UPPER){
				if(val> LOWER){
					rescale(val);
					pixel.y = val;
				}else{
					pixel.y = 0;
				}
			}
		});
		
		split(current,splitarr);

		
		
		flow = Mat(YDIM, XDIM, CV_32FC3);
		Mat conv[] = {splitarr[0],splitarr[1],splitarr[1]};
		merge(conv,3,flow);
		cvtColor(flow,flow,CV_HSV2BGR);
		imshow("Flow",flow);
		
		add(accumulator2,accumulator,accumulator);
		
		
		
		Mat out = Mat::zeros(YDIM, XDIM, CV_32FC3);
		
		out.forEach<Pixel3>([&](Pixel3& pixel, const int position[]) -> void {
			Pixel3* accptr = accumulator.ptr<Pixel3>(position[0],position[1]);

			int val = accptr->x;
			if(val > .05 * i){
				if(val < .4 * i){
					pixel.z = float(val) / i;
				}else{
					pixel.x = float(val) / i;
				}
			}else{
					pixel.y = float(val) / i;
			}
		});
		
		Mat overlay = Mat::zeros(YDIM, XDIM, CV_8UC3);
		
		//Find green surrounded by red in accumulator image
#define localwin 20
		
		for (int y = 0; y < YDIM- localwin*2; y+=localwin) {
			for (int x = 0 ; x < XDIM - localwin*2; x+=localwin) {
				int hisum = 0; int losum = 0;
				for(int k = 0; k < localwin*2; k++){
					for(int j = 0; j<localwin*2; j++){
						if(out.ptr<Pixel3>(y+j, x+k)->z){hisum++;}
						if(out.ptr<Pixel3>(y+j, x+k)->y){losum++;}
					}
				}
				if(hisum > localwin*localwin && losum > localwin*localwin){
					//printf("%d %d\n",hisum,losum);
					for(int k = 0; k < localwin*2; k++){
						for(int j = 0; j<localwin*2; j++){
							if(out.ptr<Pixel3>(y+j, x+k)->y){overlay.ptr<Pixelc>(y+j, x+k)->z ++;}
						}
					}
				}
			}
		}

		
		
		if(i>90){
			subframe[i%HISTORY].forEach<Pixelc>([&](Pixelc& pixel, const int position[]) -> void {
				Pixelc* over = overlay.ptr<Pixelc>(position[0],position[1]);
				if(over->z == 4){
					pixel.z = 255;
				}
			});
		}
	
		//imshow("Accumulator",out);
		
		imshow("Rip Current Detector",subframe[i%HISTORY]);
		
		
		video_out.write(subframe[i%HISTORY]);

		//out.convertTo(save,CV_8UC3,255);
		//videoagg.write(save);
		
		
		
		//imshow("Classifier",waterclass);
		
		waitKey(1);
		
		//flow.convertTo(save,CV_8UC3,255);
		//videoflow.write(save);
		
		//waterclass.convertTo(save,CV_8UC3,255);
		//videoclass.write(save);
		
		
		
		stable[i%STABILIZE] = Mat::zeros(YDIM, XDIM, CV_32FC2);
		
		
		
	}
	
		
		/*
		for(int j = 0; j< THRESH_BINS; j++){
			printf("%6d ,",hist[j]);
			
		}
		printf("\n------\n");
		 */
	
	flow_raw.release();
	
	//waitKey(0);
	video.release();
	video_out.release();
	//videoflow.release();
	//videoclass.release();
	//videoagg.release();
	return 0;
	
}


void wheel(){
	
	namedWindow("Color Wheel", WINDOW_AUTOSIZE );
	
	Mat foo = Mat::ones(YDIM, YDIM, CV_32FC3);
	
	foo.forEach<Pixel3>([&](Pixel3& pixel, const int position[]) -> void {
		
		float tx = (position[1]-240)/240.0;
		float ty = (240-position[0])/240.0;
		
		
		int bin = (int) floor(atan(ty/tx)/M_PI  * 18  );//Begins to calculate the angle as an integer between 1 and 35.
		
		
		
		if(ty>0){
			if(tx>0){
				bin = bin;
			}else{
				bin = 18 + bin;
			}
		}else{
			
			if(tx<0){
				bin = bin+18;
			}else{
				bin = 36 + bin;
			}
		}
		pixel.x = bin * 10;
		
		
		
		float d = sqrt(tx*tx + ty *ty);
	
		
		pixel.y = d > 1 ? 0 : d;
		pixel.z = d > 1 ? 0 : 1;
		
	});
	
	
	cvtColor(foo,foo,CV_HSV2BGR);
	imshow("Color Wheel",foo);
	
	waitKey(0);
	
	exit(0);

	
}

