// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2017 Intel Corporation. All Rights Reserved.

#include <iostream>
#include <vector>
// FFmpeg
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
}
#include <time.h>
#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <opencv2/opencv.hpp>   // Include OpenCV API
#include <opencv2/highgui.hpp>

int main(int argc, char * argv[]) try
{

    using namespace cv;
    const auto window_name = "Display Image";
    //namedWindow(window_name, WINDOW_AUTOSIZE);
    static int decoder_reorder_pts = -1;
    uint64_t   systime_at_pts_zero = 0;

    int ret;
    if (argc < 2) {
        std::cout << "Usage: rs-playback <infile>" << std::endl;
        return 1;
    }
    const char* infile = argv[1];

    // initialize FFmpeg library
    av_register_all();


    // open input file context
    AVFormatContext* inctx = nullptr;
    // inctx, infile, file_format, buffer_size, format_options
    ret = avformat_open_input(&inctx, infile, nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "fail to avforamt_open_input(\"" << infile << "\"): ret=" << ret;
        return 2;
    }
    // retrive input stream information
    ret = avformat_find_stream_info(inctx, nullptr);
    if (ret < 0) {
        std::cerr << "fail to avformat_find_stream_info: ret=" << ret;
        return 2;
    }
    // After above call, inctx->streams has been populated and can be dumped on stdio via this
    // av_dump_format(pFormatCtx, 0, argv[1], 0);

    // find primary video stream
    AVCodec* vcodec = nullptr;
    ret = av_find_best_stream(inctx, AVMEDIA_TYPE_VIDEO, -1, -1, &vcodec, 0);
    if (ret < 0) {
        std::cerr << "fail to av_find_best_stream: ret=" << ret;
        return 2;
    }
    const int vstrm_idx = ret;
    AVStream* vstrm = inctx->streams[vstrm_idx];

    // Missing code
       // avcoded_find_decoder()
    // copy context
       // avcodec_alloc_context3()
       // avcodec_copy_context()

    // open video decoder context
    ret = avcodec_open2(vstrm->codec, vcodec, nullptr);
    if (ret < 0) {
        std::cerr << "fail to avcodec_open2: ret=" << ret;
        return 2;
    }

    // print input video stream informataion
    std::cout
        << "infile: " << infile << "\n"
        << "format: " << inctx->iformat->name << "\n"
        << "vcodec: " << vcodec->name << "\n"
        << "size:   " << vstrm->codec->width << 'x' << vstrm->codec->height << "\n"
        << "fps:    " << av_q2d(vstrm->codec->framerate) << " [fps]\n"
        << "length: " << av_rescale_q(vstrm->duration, vstrm->time_base, {1,1000}) / 1000. << " [sec]\n"
        << "pixfmt: " << av_get_pix_fmt_name(vstrm->codec->pix_fmt) << "\n"
        << "frame:  " << vstrm->nb_frames << "\n"
        << std::flush;

     std::cout
        << "fps num: " << vstrm->codec->framerate.num << "\n"
        << "fps den: " << vstrm->codec->framerate.den << "\n"
        << "time-base num: " << vstrm->time_base.num << "\n"
        << "time-base den: " << vstrm->time_base.den << "\n"
        << std::flush;


    // initialize sample scaler
    const int dst_width = vstrm->codec->width;
    const int dst_height = vstrm->codec->height;
    const AVPixelFormat dst_pix_fmt = AV_PIX_FMT_BGR24;
    SwsContext* swsctx = sws_getCachedContext(
        nullptr, vstrm->codec->width, vstrm->codec->height, vstrm->codec->pix_fmt,
        dst_width, dst_height, dst_pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!swsctx) {
        std::cerr << "fail to sws_getCachedContext";
        return 2;
    }
    std::cout << "output: " << dst_width << 'x' << dst_height << ',' << av_get_pix_fmt_name(dst_pix_fmt) << std::endl;

    // allocate frame buffer for output
    AVFrame* frame = av_frame_alloc();
    std::vector<uint8_t> framebuf(avpicture_get_size(dst_pix_fmt, dst_width, dst_height));
    avpicture_fill(reinterpret_cast<AVPicture*>(frame), framebuf.data(), dst_pix_fmt, dst_width, dst_height);

    AVFrame* decframe = av_frame_alloc();
    unsigned nb_frames = 0;
    bool end_of_stream = false;
    int got_pic = 0;
    AVPacket pkt;

    // Decoding Loop
    do {
        if (!end_of_stream) {
            // read packet from input file
            ret = av_read_frame(inctx, &pkt);
            if (ret < 0 && ret != AVERROR_EOF) {
                std::cerr << "fail to av_read_frame: ret=" << ret;
                return 2;
            }
            if (ret == 0 && pkt.stream_index != vstrm_idx)
                goto next_packet;
            end_of_stream = (ret == AVERROR_EOF);
        }
        if (end_of_stream) {
            // null packet for bumping process
            av_init_packet(&pkt);
            pkt.data = nullptr;
            pkt.size = 0;
        }
        // decode video frame
        avcodec_decode_video2(vstrm->codec, decframe, &got_pic, &pkt);


        int64_t pts;
         if (decoder_reorder_pts == -1) {
             pts = av_frame_get_best_effort_timestamp(decframe);
             std::cout << "pts is: " << pts << "\n";
         } else if (decoder_reorder_pts) {
             pts = decframe->pkt_pts;
             std::cout << "reordered pts is: " << pts << "\n";
         } else {
             pts = decframe->pkt_dts;
             std::cout << "finally pts is: " << pts << "\n";
         }

         if (pts == AV_NOPTS_VALUE) {
             pts = 0;
             std::cout << "setting pts to ZERO" << "\n";
             systime_at_pts_zero = av_gettime();
         }

         // pts_to_usec
         if(pts == 0) {
            // no delay
         }
         else if(pts != 0) {
            uint64_t time_increment =  av_gettime() - systime_at_pts_zero;
            uint64_t pts_to_usec =   ( ( (pts * 1000* vstrm->time_base.num) / vstrm->time_base.den) *1000 );
            std::cout << "pts to usec is: " <<  pts_to_usec << "\n";
            std::cout << "time_increment is: " <<  time_increment << "\n";
            int64_t delay_required = pts_to_usec - time_increment;
            if(delay_required > 0){
               av_usleep(delay_required);
               std::cout << "delay needed is is: " <<  delay_required << "\n";
            }

         }



        if (!got_pic)
            goto next_packet;
        // convert frame to OpenCV matrix
        sws_scale(swsctx, decframe->data, decframe->linesize, 0, decframe->height, frame->data, frame->linesize);
        {
        cv::Mat image(dst_height, dst_width, CV_8UC3, framebuf.data(), frame->linesize[0]);
        cv::imshow("press ESC to exit", image);

/*        uint64_t time1 = av_gettime();
        std::cout << "sys time is: " <<  time1 << "\n";
        av_usleep(2000);
        time1 = av_gettime();
        std::cout << "sys time after sleep is: " <<  time1 << "\n";*/


        if (cv::waitKey(1) == 0x1b)
            break;
        }
        std::cout << nb_frames << '\r' << std::flush;  // dump progress
        ++nb_frames;
next_packet:
        av_free_packet(&pkt);
    } while (!end_of_stream || got_pic);

    // End of Stream?

    std::cout << nb_frames << " frames decoded" << std::endl;

    av_frame_free(&decframe);
    av_frame_free(&frame);
    avcodec_close(vstrm->codec);
    avformat_close_input(&inctx);
    return EXIT_SUCCESS;
}
catch (const rs2::error & e)
{
    std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
    return EXIT_FAILURE;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}



