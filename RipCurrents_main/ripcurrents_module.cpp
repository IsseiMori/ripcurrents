#include <math.h>
#include <stdio.h>
#include <sys/time.h>

#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>  //Actually opencv3.2, in spite of the name
#include <opencv2/optflow/motempl.hpp>

#include "ripcurrents.hpp"

// Mat streamfield		-input: how far it moved
// Mat streamoverlay_color		-output color
void streamline_displacement(Mat& streamfield, Mat& streamoverlay_color){
	//Compute streamline length
	double lenmax;
	minMaxLoc(streamfield,NULL,&lenmax,NULL,NULL);
	(streamfield).convertTo(streamoverlay_color,CV_8UC1,255/lenmax);
	applyColorMap(streamoverlay_color, streamoverlay_color, COLORMAP_JET);
}

// Mat streamlines_distance		-input: how far it has moved
// Mat streamoverlay_color		-output color
void streamline_total_motion(Mat& streamlines_distance, Mat& streamoverlay_color){
	double distmax;
	minMaxLoc(streamlines_distance,NULL,&distmax,NULL,NULL);
	streamlines_distance.convertTo(streamoverlay_color,CV_8UC1,255/distmax);
	applyColorMap(streamoverlay_color, streamoverlay_color, COLORMAP_JET);
}

// Mat streamfield		-input: how far it moved
// Mat streamlines_distance		-input: how far it has moved
// Mat streamoverlay_color		-output color
// pre: streamline_displacement(), streamline_total_motion()
void streamline_ratio(Mat& streamfield, Mat& streamlines_distance, Mat& streamoverlay_color){
	divide(streamfield,streamlines_distance,streamoverlay_color);
	double ratiomax;
	minMaxLoc(streamoverlay_color,NULL,&ratiomax,NULL,NULL);
	streamoverlay_color.convertTo(streamoverlay_color,CV_8UC1,255/ratiomax);
	applyColorMap(streamoverlay_color, streamoverlay_color, COLORMAP_JET);
}

// Mat streamlines_mat		-input
// Mat streamline_density		-output
void streamline_positions(Mat& streamlines_mat, Mat& streamline_density){
	for (int y = 0; y < YDIM; y++) {
		Pixel2* ptr = streamlines_mat.ptr<Pixel2>(y, 0);
		const Pixel2* ptr_end = ptr + (int)XDIM;
		for (int x = 0 ; ptr != ptr_end; ++ptr, x++) {
			int xind = (int) roundf(floor(ptr->x + x));
			int yind = (int) roundf(floor(ptr->y + y));
			if(xind < 1 || yind < 1 || xind + 2 > streamline_density.cols || yind  + 2 > streamline_density.rows)  //Verify array bounds
				{continue;}

			Pixel3 * density_ptr = streamline_density.ptr<Pixel3>(yind,xind);
			(*density_ptr) = Pixel3(1,1,1);
			
		}
	}
}

// Mat streamout		-output
// Mat streamoverlay_color
// Mat streamoverlay
// int streamlines		-number of streamline track point
// Pixel2 streampt[]	-array of streamline track point
// int framecount
// int totalframes
// Mat current
// float UPPER
// float prop_above_upper
void get_streamlines(Mat& streamout, Mat& streamoverlay_color, Mat& streamoverlay, int streamlines, Pixel2 streampt[], int framecount, int totalframes, Mat& current, float UPPER, float prop_above_upper[]){
	for(int s = 0; s < streamlines; s++){
		streamline(streampt+s, Scalar(framecount*(255.0/totalframes)), current, streamoverlay, 2, 1,UPPER,prop_above_upper);
	}
	
	applyColorMap(streamoverlay, streamoverlay_color, COLORMAP_RAINBOW);
	add(streamoverlay_color, streamout, streamout, streamoverlay, -1);
}

// Mat current
// int hist[]		-
// int histsum		-
// int hist2d[][]		-
// int histsum2d[]		-
// float UPPER		-
// float UPPER2d[]		-
// float prop_above_upper[]		-
void create_histogram(Mat current, int hist[HIST_BINS], int& histsum, int hist2d[HIST_DIRECTIONS][HIST_BINS]
	 ,int histsum2d[HIST_DIRECTIONS], float& UPPER, float UPPER2d[HIST_DIRECTIONS], float prop_above_upper[HIST_DIRECTIONS]){
	
	//Construct histograms to get thresholds
	//Figure out what "slow" or "fast" is
	for (int y = 0; y < YDIM; y++) {
		Pixel3* ptr = current.ptr<Pixel3>(y, 0);
		//const Pixel3* ptr_end = ptr + (int)XDIM;
		const Pixel3* ptr_end = current.ptr<Pixel3>(y+1, 0);
		for (; ptr < ptr_end; ++ptr) {
			int bin = (ptr->y) * HIST_RESOLUTION;
			int angle = (ptr->x * HIST_DIRECTIONS)/ 360; //order matters, truncation
			if(bin < HIST_BINS &&  bin >= 0){
				hist[bin]++; histsum++;
				hist2d[angle][bin]++; histsum2d[angle]++;
			}
		}
	}
	
	//Use histogram to create overall threshold
	int threshsum = 0;
	int bin = HIST_BINS-1;
	while(threshsum < (histsum*.05)){
		threshsum += hist[bin];
		bin--;
	}
	UPPER = bin/float(HIST_RESOLUTION);
	int targetbin = bin;
	
	
	//As above, but per-direction
	//This is more of a visual aid, allowing small motion in directions with 
	//little movement to not be drowned out by large-scale motion
	for(int angle = 0; angle < HIST_DIRECTIONS; angle++){
		int threshsum2 = 0;
		int bin = HIST_BINS-1;
		while(threshsum2 < (histsum2d[angle]*.05)){
			threshsum2 += hist2d[angle][bin];
			bin--;
		}
		UPPER2d[angle] = bin/float(HIST_RESOLUTION);
		if(UPPER2d[angle] < 0.01) {UPPER2d[angle] = 0.01;}//avoid division by 0
		
		int threshsum3 = 0;
		bin = HIST_BINS-1;
		while(bin > targetbin){
			threshsum3 += hist2d[angle][bin];
			bin--;
		}
		prop_above_upper[angle] = ((float)threshsum3)/threshsum;
		
		
		//printf("Angle %d; prop_above_upper: %f\n",angle,prop_above_upper[angle]);
	}
}

// Mat current
// Mat waterclass
// Mat accumulator2
// UPPER
// MID
// LOWER
// UPPER2d[]
void create_flow(Mat current, Mat waterclass, Mat accumulator2, float UPPER, float MID, float LOWER, float UPPER2d[HIST_DIRECTIONS]){
	//Classify into fast and slow movement
	current.forEach<Pixel3>([&](Pixel3& pixel, const int position[]) -> void {
		Pixel3* classptr = waterclass.ptr<Pixel3>(position[0],position[1]);
		Pixel3 * pt = accumulator2.ptr<Pixel3>(position[0],position[1]);
		int angle = (pixel.x * HIST_DIRECTIONS)/ 360; //order matters, truncation
		float val = pixel.z ;
		
		//Load classifier and overlay
		if(val > UPPER){classptr->x = .5; pt->x++;}else{
			if(val > MID){classptr->z = 1;}else{
				if(val > LOWER){classptr->z = .5;}else{
						{classptr->y = .5;}
				}
			}
		}
		
		
		//Rescale for display
		//pixel.z = 1;
		pixel.z = val/UPPER2d[angle];
		if(pixel.z > 1){
			pixel.y = 1;
		} else {
			pixel.y = .7;
		}
		
		
	});
}

// Mat accumulator
// Mat accumulator2
// Mat out
// Mat outmask
// int framecount
void create_accumulationbuffer(Mat& accumulator, Mat accumulator2, Mat& out, Mat outmask, int framecount){
//For every pixel, if it has been identified as fast(possibly a wave), increment a counter.
	if(framecount > 30){
		add(accumulator2,accumulator,accumulator);
	}
	
	//Visualize the accumulated waves
	out.forEach<Pixel3>([&](Pixel3& pixel, const int position[]) -> void {
		Pixel3* accptr = accumulator.ptr<Pixel3>(position[0],position[1]);
		uchar* maskptr = outmask.ptr<uchar>(position[0],position[1]);
		
		int val = accptr->x;
		if(val > .1 * framecount){
			if(val < .2 * framecount){
				pixel.z = 1;
			}else{
				pixel.x = 1;
			}
		}else{
				pixel.y = .5;
				*maskptr = 255;
		}
	});
}

// Mat outmask
// pre: accumulationbuffer()
void create_edges(Mat& outmask){
	Mat morph_window = getStructuringElement(MORPH_ELLIPSE, Size(5,5));
	dilate(outmask, outmask, morph_window);
	morphologyEx( outmask, outmask, 4, morph_window ); //Morphology operation #4: gradient (dilation - erosion)	
}

// Mat subframe		:output
// Mat outmask		:input
// pre: accumulationbuffer(), create_edges()
void create_output(Mat& subframe, Mat outmask){
	Mat overlay = Mat::zeros(Size(XDIM, YDIM), CV_8UC3);
	
	//Combine edges and original
	if(true/*framecount>90*/){
		subframe.forEach<Pixelc>([&](Pixelc& pixel, const int position[]) -> void {
			uchar over =  *outmask.ptr<uchar>(position[0],position[1]);
			//uchar stream =  *streamoverlay.ptr<uchar>(position[0],position[1]);
			if(over){
				pixel.z = 255;
			}
			//if(stream){
			//	pixel.x = 255;
			//	pixel.y = 0;
			//	pixel.z = 255;
			//}
			
		});
	}
}

void display_histogram(int hist2d[HIST_DIRECTIONS][HIST_BINS],int histsum2d[HIST_DIRECTIONS]
					,float UPPER2d[HIST_DIRECTIONS], float UPPER, float prop_above_upper[HIST_DIRECTIONS]){
	namedWindow("Color Histogram", WINDOW_AUTOSIZE );
	
	Mat foo = Mat::ones(480, 480, CV_32FC3);
	
	foo.forEach<Pixel3>([&](Pixel3& pixel, const int position[]) -> void {
		
		
		float tx = (position[1]-240.0)/240.0;
		float ty = (position[0]-240.0)/240.0;
		
		float theta = atan2(ty,tx)*180/M_PI;//find angle
		theta += theta < 0 ? 360 : 0; //enforce strict positive angle
		float r = sqrt(tx*tx + ty*ty);
		
		int direction = ((int)((theta * HIST_DIRECTIONS)/360));
		pixel.x = direction * 360/HIST_DIRECTIONS;
		
		//float proportion = ((float)hist2d[direction][(int)(r*HIST_BINS)])/histsum2d[direction];
		
		pixel.y = r > UPPER2d[direction]*HIST_RESOLUTION/HIST_BINS ? 0 : 1;
		pixel.z = r > prop_above_upper[direction]*10 ? 0 : 1;
	});
	
	
	cvtColor(foo,foo,CV_HSV2BGR);
	imshow("Color Histogram",foo);
	
	return;

}

void stabilizer(Mat current, Mat current_prev){
	double sum_x=0, sum_y=0;
	int cx=0, cy=0;
	int diff = 0;
	for ( int row = (int)(current.rows * 0.9); row < current.rows; row++ ){
		Pixel2* ptr = current.ptr<Pixel2>(row, (int)(current.cols * 0.9));
		cy++;
		cx=0;
		for ( int col = (int)(current.cols * 0.9); col < current.cols; col++){
			sum_x += ptr->x;
			sum_y += ptr->y;
			cx++;
			ptr++;
		}
	}

	double mean_x = sum_x / cx;
	double mean_y = sum_y / cy;

	printf("x %f, y %f \n", mean_x, mean_y);
	
	for ( int row = 0; row < current.rows; row++ ){
		Pixel2* ptr = current.ptr<Pixel2>(row, 0);
		for ( int col = 0; col < current.cols; col++){
			if(ptr->x != 0) ptr->x -= mean_x * 0.2;
			if(ptr->y != 0) ptr->y -= mean_y * 0.2;
			ptr++;
		}
	}
}

void globalOrientation(UMat u_f1, UMat u_f2, Mat& hist_gray){
	Mat color_diff;
	absdiff(u_f1, u_f2, color_diff);
	Mat black_diff;
	threshold(color_diff, black_diff, 30, 1, THRESH_BINARY);
	clock_t proc_time = clock();
	Mat motion_history = Mat::zeros(Size(XDIM, YDIM), CV_32FC1);
	motempl::updateMotionHistory(black_diff, motion_history, proc_time, 1);
	// min max normalize
	double max, min;
	minMaxLoc(motion_history, &min, &max);
	motion_history = motion_history / max;
	Mat mask, orientation;
	motempl::calcMotionGradient(motion_history, mask, orientation, 0.25, 1, 3);
	double angle_deg = motempl::calcGlobalOrientation(orientation, mask, motion_history, proc_time, 1);
	printf("%lf\n", angle_deg);
	Mat hist_color = (motion_history ) * 255;
	hist_color.convertTo(hist_color, CV_8U);
	cvtColor(hist_color,hist_gray,COLOR_GRAY2BGR);

	// draw center point
	circle(hist_gray, Point((int)(XDIM / 2), (int)(YDIM / 2)), 3, Scalar(0, 215, 255), CV_FILLED, 16, 0);

	// draw line
	double angle_rad = angle_deg * M_PI / 180;
	arrowedLine(hist_gray, Point((int)(XDIM / 2), (int)(YDIM / 2)), 
		Point((int)(XDIM / 2 + cos(angle_rad) * 10), (int)(YDIM / 2 + sin(angle_rad) * 50)),
		Scalar(0, 215, 255), 2, 16, 0, 0.2);

	// draw dots
	for ( int row = 0; row < YDIM; row += 30 ){
		for ( int col = 0; col < XDIM; col += 30 ){
			circle(hist_gray, Point(col, row), 1, Scalar(0, 215, 0), CV_FILLED, 16, 0);
			angle_deg = orientation.at<double>(row, col);
			if ( angle_deg > 0 ) angle_rad = angle_deg * M_PI / 180;
			arrowedLine(hist_gray, Point(col, row), 
				Point((int)(col + cos(angle_rad) * 10), (int)(row + sin(angle_rad) * 10)),
				Scalar(0, 215, 0), 1, 16, 0, 0.4);
		}
	}
}

// subframe - bgr image of current frame
// buffer_hsv - store hsv format data of previous BUFFER_COUNT frames
// update_ith_buffer - number of element in buffer array to update
// average_hsv - average hsv of buffer_hsv
void averageHSV(Mat& subframe, std::vector<Mat> buffer_hsv, int update_ith_buffer, Mat& average_hsv){
	Mat hsv;
	//cvtColor(subframe, hsv, COLOR_HSV2BGR);

	// subtract old buffer data from average
	average_hsv -= buffer_hsv[update_ith_buffer];

	// get new buffer
	buffer_hsv[update_ith_buffer] = subframe / BUFFER_FRAME;
	// add new buffer to average
	average_hsv += buffer_hsv[update_ith_buffer];
}

// buffer - store previous BUFFER_COUNT frames
// current - frame data
// update_ith_buffer - number of element in buffer array to update
// average - store the average vector data
// average_color - convert the vector data to hsv format image
// grid - average of average in small grid
// max_displacement - store the max displacement of vector
// UPPER - histogram data to get clear result
void averageVector(std::vector<Mat> buffer, Mat& current, int update_ith_buffer, Mat& average, Mat& average_color, double** grid, float max_displacement, float UPPER) {
	// number of rows and cols in each grid
	int grid_col_num = (int)(XDIM/GRID_COUNT);
	int grid_row_num = (int)(YDIM/GRID_COUNT);
	
	// subtract old buffer data from average
	average -= buffer[update_ith_buffer] / BUFFER_FRAME;
	buffer[update_ith_buffer] = Mat::zeros(YDIM,XDIM,CV_32FC2);
	// get new buffer
	buffer[update_ith_buffer].forEach<Pixel2>([&](Pixel2& pixel, const int position[]) -> void{
		get_delta(&pixel, position[1],position[0], current, 2, UPPER);
	});

	// add new buffer to average
	average += buffer[update_ith_buffer] / BUFFER_FRAME;

	float global_theta = 0;
	float global_magnitude = 0;

	int grid_row = 1, grid_col = 1;

	for ( int i = 0; i < GRID_COUNT; i++ ) {
		for ( int j = 0; j < GRID_COUNT; j++ ) {
			grid[i][j] = 0;
		}
	}

	// store vector data of average
	int co = 0;
	for ( int row = 0; row < average.rows; row++ ) {
		Pixel2* ptr = average.ptr<Pixel2>(row, 0);
		Pixelc* ptr2 = average_color.ptr<Pixelc>(row, 0);

		if ( row >= grid_row_num * grid_row)
			grid_row++;
		
		grid_col = 1;

		for ( int col = 0; col < average.cols; col++ ) {
			float theta = atan2(ptr->y, ptr->x)*180/M_PI;	// find angle
			theta += theta < 0 ? 360 : 0;	// enforce strict positive angle
			
			// store vector data
			ptr2->x = theta / 2;
			ptr2->y = 255;
			ptr2->z = sqrt(ptr->x * ptr->x + ptr->y * ptr->y)*255/max_displacement;
			//if ( ptr2->z < 30 ) ptr2->z = 0;

			// store the previous max to maxmin next frame
			if ( sqrt(ptr->x * ptr->x + ptr->y * ptr->y) > max_displacement ) max_displacement = sqrt(ptr->x * ptr->x + ptr->y * ptr->y);

			global_theta += ptr2->x * ptr2->z;
			global_magnitude += ptr2->z;

			if ( col >= grid_col_num * grid_col)
				grid_col++;

			// add the vector to the corresponding grid
			grid[grid_row-1][grid_col-1] += theta;
			if (grid_col == 5 && grid_row == 5){
				co++;
			}

			ptr++;
			ptr2++;
		}
	}

	// draw global orientation arrow
	circle(average_color, Point((int)(XDIM/2), (int)(YDIM/2)), 3, Scalar(0, 215, 255), CV_FILLED, 16, 0);
	double global_angle_rad = global_theta * 2 / global_magnitude * M_PI / 180;
	arrowedLine(average_color, Point((int)(XDIM / 2), (int)(YDIM / 2)), 
		Point((int)(XDIM / 2 + cos(global_angle_rad) * 10), (int)(YDIM / 2 + sin(global_angle_rad) * 50)),
		Scalar(0, 215, 255), 2, 16, 0, 0.2);

	// show as hsv format
	cvtColor(average_color, average_color, CV_HSV2BGR);

	// draw arrows for each grid
	for ( int row = 1; row < GRID_COUNT; row++ ){
		for ( int col = 1; col < GRID_COUNT; col++ ){
			double angle_deg = grid[row][col] / co;
			double angle_rad = angle_deg  * M_PI / 180;
			// find in-between angle
			double angle_between = min(abs(angle_rad - global_angle_rad), 2*M_PI-abs(angle_rad - global_angle_rad));
			if ( angle_between > M_PI * 0.7 ) {
				circle(average_color, Point(col * grid_col_num, row * grid_row_num), 1, Scalar(0, 255, 0), CV_FILLED, 16, 0);
				arrowedLine(average_color, Point(col * grid_col_num, row * grid_row_num), 
					Point((int)(col * grid_col_num + cos(angle_rad) * 10), (int)(row * grid_row_num + sin(angle_rad) * 10)),
					Scalar(0, 255, 0), 1, 16, 0, 0.4);
			} /*else {
				circle(average_color, Point(col * grid_col_num, row * grid_row_num), 1, Scalar(255, 0, 0), CV_FILLED, 16, 0);
				arrowedLine(average_color, Point(col * grid_col_num, row * grid_row_num), 
					Point((int)(col * grid_col_num + cos(angle_rad) * 10), (int)(row * grid_row_num + sin(angle_rad) * 10)),
					Scalar(255, 0, 0), 1, 16, 0, 0.4);
			}*/
		}
	}
}

void streamline(Pixel2 * pt, cv::Scalar color, cv::Mat flow, cv::Mat overlay, float dt, int iterations, float UPPER, float prop_above_upper[HIST_DIRECTIONS]){
	
	
	for( int i = 0; i< iterations; i++){
		
		float x = pt->x;
		float y = pt->y;
		
		int xind = (int) floor(x);
		int yind = (int) floor(y);
		float xrem = x - xind;
		float yrem = y - yind;
		
		if(xind < 1 || yind < 1 || xind + 2 > flow.cols || yind  + 2 > flow.rows)  //Verify array bounds
		{
			return;
		}
		
		//Bilinear interpolation
		Pixel2 delta =		(*flow.ptr<Pixel2>(yind,xind))		* (1-xrem)*(1-yrem) +
		(*flow.ptr<Pixel2>(yind,xind+1))	* (xrem)*(1-yrem) +
		(*flow.ptr<Pixel2>(yind+1,xind))	* (1-xrem)*(yrem) +
		(*flow.ptr<Pixel2>(yind+1,xind+1))	* (xrem)*(yrem) ;
		
		
		float theta = atan2(delta.y,delta.x)*180/M_PI;//find angle
		theta += theta < 0 ? 360 : 0; //enforce strict positive angle
		int direction = ((int)((theta * HIST_DIRECTIONS)/360));
		//if(prop_above_upper[direction] > .05){return;}
		
		float r = sqrt(delta.x*delta.x + delta.y*delta.y);
		if(r > UPPER){return;}
		
		
		Pixel2 newpt = *pt + delta*dt/iterations;
		
		cv::line(overlay,* pt, newpt, color, 1, 8, 0);
		
		*pt = newpt;
	}
	
	return;
}

void streamline_field(Pixel2 * pt, float* distancetraveled, int xoffset, int yoffset, cv::Mat flow, float dt, int iterations, float UPPER, float prop_above_upper[HIST_DIRECTIONS]){
	
	
	for( int i = 0; i< iterations; i++){
		
		float x = pt->x + xoffset;
		float y = pt->y + yoffset;
		
		int xind = (int) floor(x);
		int yind = (int) floor(y);
		float xrem = x - xind;
		float yrem = y - yind;
		
		if(xind < 1 || yind < 1 || xind + 2 > flow.cols || yind  + 2 > flow.rows)  //Verify array bounds
		{
			return;
		}
		
		//Bilinear interpolation
		Pixel2 delta =		(*flow.ptr<Pixel2>(yind,xind))		* (1-xrem)*(1-yrem) +
		(*flow.ptr<Pixel2>(yind,xind+1))	* (xrem)*(1-yrem) +
		(*flow.ptr<Pixel2>(yind+1,xind))	* (1-xrem)*(yrem) +
		(*flow.ptr<Pixel2>(yind+1,xind+1))	* (xrem)*(yrem) ;
		
		
		float theta = atan2(delta.y,delta.x)*180/M_PI;//find angle
		theta += theta < 0 ? 360 : 0; //enforce strict positive angle
		int direction = ((int)((theta * HIST_DIRECTIONS)/360));
		//if(prop_above_upper[direction] > .05){return;}
		
		float r = sqrt(delta.x*delta.x + delta.y*delta.y);
		if(r > UPPER){return;}
		
		Pixel2 newpt = *pt + delta*dt/iterations;
		*distancetraveled = *distancetraveled + r;
		
		*pt = newpt;
	}
	
	return;
}

void get_delta(Pixel2 * pt, int xoffset, int yoffset, cv::Mat flow, float dt, float UPPER){
	
	float x = pt->x + xoffset;
	float y = pt->y + yoffset;
	
	int xind = (int) floor(x);
	int yind = (int) floor(y);
	float xrem = x - xind;
	float yrem = y - yind;
	
	if(xind < 1 || yind < 1 || xind + 2 > flow.cols || yind  + 2 > flow.rows)  //Verify array bounds
	{
		return;
	}
	
	//Bilinear interpolation
	Pixel2 delta =		(*flow.ptr<Pixel2>(yind,xind))		* (1-xrem)*(1-yrem) +
	(*flow.ptr<Pixel2>(yind,xind+1))	* (xrem)*(1-yrem) +
	(*flow.ptr<Pixel2>(yind+1,xind))	* (1-xrem)*(yrem) +
	(*flow.ptr<Pixel2>(yind+1,xind+1))	* (xrem)*(yrem) ;
	
	float r = sqrt(delta.x*delta.x + delta.y*delta.y);
	if(r > UPPER){return;}
	
	Pixel2 newpt = *pt + delta*dt;
	
	*pt = newpt;
	
	return;
}

double timediff(){
	static struct timeval oldtime;
	struct timeval newtime;
	gettimeofday(&newtime,NULL);
	double diff = (newtime.tv_sec - oldtime.tv_sec) + (newtime.tv_usec - oldtime.tv_usec)/1000000.0;
	gettimeofday(&oldtime,NULL);
	return diff;
}