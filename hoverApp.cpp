
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

static char mediaSetup[] = "media-ctl -d /dev/media0 --set-v4l2 \'\"imx219 10-0010\":0[fmt:SRGGB8_1X8/1920x1080]\'";
static const char v4l2Setup[] = "v4l2-ctl -d /dev/video0 --set-fmt-video width=1920,height=1080,pixelformat=RGGB";
static const char ispInSetup[] = "v4l2-ctl -d /dev/video13 --set-fmt-video-out width=1920,height=1080,pixelformat=RGGB,quantization=lim-range";
static const char ispOutSetup[] = "v4l2-ctl -d /dev/video14 --set-fmt-video width=1920,height=1080,pixelformat=RGB3";

#ifndef V4L2_PIX_FMT_H264
#define V4L2_PIX_FMT_H264     v4l2_fourcc('H', '2', '6', '4') /* H264 with start codes */
#endif

#ifndef V4L2_PIX_FMT_SRGB10
#define V4L2_PIX_FMT_SRGB10     v4l2_fourcc('R', 'G', '1', '0') /* SRGB10 with start codes */
#endif

enum io_method {
        IO_METHOD_READ,
        IO_METHOD_MMAP,
        IO_METHOD_USERPTR,
};

struct buffer {
        void   *start;
        size_t  length;
};

static const char            *dev_name;
static const char            *dev_isp_in;
static const char            *dev_isp_out;
static enum io_method   io = IO_METHOD_MMAP;
static int              fd = -1;
static int              fd_isp_in = -1;
static int              fd_isp_out = -1;
struct buffer          *buffers;
struct buffer          *buffers_isp_in;
struct buffer          *buffers_isp_out;
static unsigned int     n_buffers;
static int              out_buf;
static int              force_format;
static int              frame_count = 200;
static int              frame_number = 0;

static void errno_exit(const char *s)
{
        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
        exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg)
{
        int r;

        do {
                r = ioctl(fh, request, arg);
        } while (-1 == r && EINTR == errno);

        return r;
}

static void process_image(const void *p, int size)
{
        frame_number++;
        char filename[22];
        sprintf(filename, "frame-%d.raw", frame_number);
        FILE *fp=fopen(filename,"wb");
        if (out_buf)
        {
        		printf("Write the image buffer to cache\n");
                fwrite(p, size, 1, fp);
        }
        fflush(fp);
        fclose(fp);
}


static int read_isp(struct v4l2_buffer buf)
{

	struct v4l2_buffer buf_isp;
	enum v4l2_buf_type type;
	switch (io) {

		case IO_METHOD_MMAP:
			printf("Step 3 \n");
			/* Copy the data into the ISP input buffecd /So	r */
			memcpy(buffers_isp_in->start, buffers[buf.index].start, buf.bytesused);
			// Queue the buffer
			printf("Step 2 \n");
			CLEAR(buf_isp);
			buf_isp.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
			buf_isp.memory = V4L2_MEMORY_MMAP;
			buf_isp.index = buf.index;
			if (-1 == xioctl(fd_isp_in, VIDIOC_QBUF, &buf_isp))
					errno_exit("VIDIOC_QBUF");
			// Queue the output buffers
			printf("Step 1 \n");
			CLEAR(buf_isp);
			buf_isp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf_isp.memory = V4L2_MEMORY_MMAP;
			buf_isp.index = buf.index;
			if (-1 == xioctl(fd_isp_out, VIDIOC_QBUF, &buf_isp))
					errno_exit("VIDIOC_QBUF");
			/* Start the stream of the ISP*/
			printf("Step 5 \n");
			type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
			if (-1 == xioctl(fd_isp_in, VIDIOC_STREAMON, &type))
					errno_exit("VIDIOC_STREAMON");
			printf("Step 5 \n");
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (-1 == xioctl(fd_isp_out, VIDIOC_STREAMON, &type))
					errno_exit("VIDIOC_STREAMON");
			/* Get the ISP processed image out */
			printf("Step 6 \n");
			CLEAR(buf_isp);
			buf_isp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf_isp.memory = V4L2_MEMORY_MMAP;
			for(;;)
			{
				if (-1 == xioctl(fd_isp_out, VIDIOC_DQBUF, &buf_isp))
				{
					switch(errno)
					{
						case EAGAIN:
	     			          continue;

					    case EIO:
					           /*Could ignore EIO, see spec.
		  		              fall through*/
					    default:
    			             errno_exit("VIDIOC_DQBUF");

					}
				}
				else
					break;
			}
			printf("Got image of size %d from %x\n",buf_isp.bytesused, buffers_isp_out->start);
			process_image(buffers_isp_out->start, buf_isp.bytesused);

			/* Queue a buffer so the data can be read through the ISP output */
			buf_isp.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
			buf_isp.memory = V4L2_MEMORY_MMAP;
			buf_isp.index = buf.index;
			if (-1 == xioctl(fd_isp_in, VIDIOC_DQBUF, &buf_isp))
					errno_exit("VIDIOC_DQBUF");
			break;
		case IO_METHOD_READ:
		case IO_METHOD_USERPTR:
		default:
				break;

	}
	return 1;
}

static int read_frame(struct v4l2_buffer *in_buf)
{
		struct v4l2_buffer buf;
        enum v4l2_buf_type type;
        unsigned int i;

        switch (io) {
        case IO_METHOD_READ:
                if (-1 == read(fd, buffers[0].start, buffers[0].length)) {
                        switch (errno) {
                        case EAGAIN:
                                return 0;

                        case EIO:
                                /* Could ignore EIO, see spec. */

                                /* fall through */

                        default:
                                errno_exit("read");
                        }
                }

                process_image(buffers[0].start, buffers[0].length);
                break;

        case IO_METHOD_MMAP:
                CLEAR(buf);
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                printf("Dequeue the frame\n");
                if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
                        switch (errno) {
                        case EAGAIN:
                                return 0;

                        case EIO:
                                /* Could ignore EIO, see spec. */

                                /* fall through */

                        default:
                                errno_exit("VIDIOC_DQBUF");
                        }
                }

                assert(buf.index < n_buffers);
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
						errno_exit("VIDIOC_STREAMOFF");
                break;

        case IO_METHOD_USERPTR:
                CLEAR(buf);

                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_USERPTR;

                if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
                        switch (errno) {
                        case EAGAIN:
                                return 0;

                        case EIO:
                                /* Could ignore EIO, see spec. */

                                /* fall through */

                        default:
                                errno_exit("VIDIOC_DQBUF");
                        }
                }

                for (i = 0; i < n_buffers; ++i)
                        if (buf.m.userptr == (unsigned long)buffers[i].start
                            && buf.length == buffers[i].length)
                                break;

                assert(i < n_buffers);

                process_image((void *)buf.m.userptr, buf.bytesused);

                if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                        errno_exit("VIDIOC_QBUF");
                break;
        }
        *in_buf = buf;
        return 1;
}

static void mainloop(void)
{
        unsigned int count;
        struct v4l2_buffer buf;
        enum v4l2_buf_type type;
        count = frame_count;

        while (count-- > 0) {
        		printf("Retrieve Frame %d\n", frame_count - count);
				type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
						errno_exit("VIDIOC_STREAMON");
                for (;;) {
                        fd_set fds;
                        struct timeval tv;
                        int r;

                        FD_ZERO(&fds);
                        FD_SET(fd, &fds);

                        /* Timeout. */
                        tv.tv_sec = 2;
                        tv.tv_usec = 0;

                        r = select(fd + 1, &fds, NULL, NULL, &tv);

                        if (-1 == r) {
                                if (EINTR == errno)
                                        continue;
                                printf("Got a select Error!\n");
                                errno_exit("select");
                        }

                        if (0 == r) {
                                fprintf(stderr, "select timeout\n");
                                printf("Got a select timeout!\n");
                                exit(EXIT_FAILURE);
                        }

                        if (read_frame(&buf))
                        {
                        	if (read_isp(buf))
                        	{
                        		if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                        			errno_exit("VIDIOC_QBUF");\
                        		break;
                        	}
                        /* EAGAIN - continue select loop. */
                        }
                }
        }
}

static void stop_capturing(void)
{
        enum v4l2_buf_type type;

        switch (io) {
        case IO_METHOD_READ:
                /* Nothing to do. */
                break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
                        errno_exit("VIDIOC_STREAMOFF");
                break;
        }
}

static void start_capturing(void)
{
        unsigned int i;
        enum v4l2_buf_type type;

        switch (io) {
        case IO_METHOD_READ:
                /* Nothing to do. */
                break;

        case IO_METHOD_MMAP:
                for (i = 0; i < n_buffers; ++i) {
                        struct v4l2_buffer buf;

                        CLEAR(buf);
                        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        buf.memory = V4L2_MEMORY_MMAP;
                        buf.index = i;

                        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                                errno_exit("VIDIOC_QBUF");
                }
                break;

        case IO_METHOD_USERPTR:
                for (i = 0; i < n_buffers; ++i) {
                        struct v4l2_buffer buf;

                        CLEAR(buf);
                        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        buf.memory = V4L2_MEMORY_USERPTR;
                        buf.index = i;
                        buf.m.userptr = (unsigned long)buffers[i].start;
                        buf.length = buffers[i].length;

                        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                                errno_exit("VIDIOC_QBUF");
                }
                break;
        }
}

static void uninit_device(void)
{
        unsigned int i;

        switch (io) {
        case IO_METHOD_READ:
                free(buffers[0].start);
                break;

        case IO_METHOD_MMAP:
                for (i = 0; i < n_buffers; ++i)
                        if (-1 == munmap(buffers[i].start, buffers[i].length))
                                errno_exit("munmap");
                break;

        case IO_METHOD_USERPTR:
                for (i = 0; i < n_buffers; ++i)
                        free(buffers[i].start);
                break;
        }

        free(buffers);
}

static void init_read(unsigned int buffer_size)
{
        buffers = (struct buffer *)calloc(1, sizeof(*buffers));

        if (!buffers) {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }

        buffers[0].length = buffer_size;
        buffers[0].start = malloc(buffer_size);

        if (!buffers[0].start) {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }
}

static void init_mmap(void)
{
        struct v4l2_requestbuffers req;

        CLEAR(req);

        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        printf("Request Memory buffer \n");

        if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        printf("%s does not support "
                                 "memory mapping\n", dev_name);
                        exit(EXIT_FAILURE);
                } else {
                        errno_exit("VIDIOC_REQBUFS");
                }
        }

        if (req.count < 2) {
                printf("Insufficient buffer memory on %s\n",
                         dev_name);
                exit(EXIT_FAILURE);
        }
        printf("calloc user buffer array\n");
        buffers = (struct buffer *)calloc(req.count, sizeof(*buffers));

        if (!buffers) {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
                struct v4l2_buffer buf;

                CLEAR(buf);

                buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory      = V4L2_MEMORY_MMAP;
                buf.index       = n_buffers;

                if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
                        errno_exit("VIDIOC_QUERYBUF");
                printf("Memory map element %d\n", n_buffers);
                buffers[n_buffers].length = buf.length;
                buffers[n_buffers].start =
                        mmap(NULL /* start anywhere */,
                              buf.length,
                              PROT_READ | PROT_WRITE /* required */,
                              MAP_SHARED /* recommended */,
                              fd, buf.m.offset);

                if (MAP_FAILED == buffers[n_buffers].start)
                        errno_exit("mmap");
        }
}

static void init_userp(unsigned int buffer_size)
{
        struct v4l2_requestbuffers req;

        CLEAR(req);

        req.count  = 4;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf(stderr, "%s does not support "
                                 "user pointer i/o\n", dev_name);
                        exit(EXIT_FAILURE);
                } else {
                        errno_exit("VIDIOC_REQBUFS");
                }
        }

        buffers = (struct buffer *)calloc(4, sizeof(*buffers));

        if (!buffers) {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
                buffers[n_buffers].length = buffer_size;
                buffers[n_buffers].start = malloc(buffer_size);

                if (!buffers[n_buffers].start) {
                        fprintf(stderr, "Out of memory\n");
                        exit(EXIT_FAILURE);
                }
        }
}

static void init_mmap_isp(void)
{
        /* Request Input buffers to send to ISP */
        struct v4l2_requestbuffers req;

        CLEAR(req);

        req.count = 1;
        req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        req.memory = V4L2_MEMORY_MMAP;
        printf("Request Memory buffer \n");

        if (-1 == xioctl(fd_isp_in, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        printf("%s does not support "
                                 "memory mapping\n", dev_name);
                        exit(EXIT_FAILURE);
                } else {
                        errno_exit("VIDIOC_REQBUFS");
                }
        }
        printf("calloc user buffer array\n");
        buffers_isp_in = (struct buffer *)calloc(req.count, sizeof(*buffers_isp_in));

        if (!buffers_isp_in) {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type        = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		buf.memory      = V4L2_MEMORY_MMAP;
		buf.index       = 0;

		if (-1 == xioctl(fd_isp_in, VIDIOC_QUERYBUF, &buf))
				errno_exit("VIDIOC_QUERYBUF");
		printf("Memory map element %d\n", n_buffers);
		buffers_isp_in->length = buf.length;
		buffers_isp_in->start =
				mmap(NULL /* start anywhere */,
					  buf.length,
					  PROT_READ | PROT_WRITE /* required */,
					  MAP_SHARED /* recommended */,
					  fd_isp_in, buf.m.offset);

		if (MAP_FAILED == buffers_isp_in->start)
				errno_exit("mmap");

        /* Request Output buffers to send to ISP */
        CLEAR(req);

        req.count = 1;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        printf("Request Memory buffer \n");

        if (-1 == xioctl(fd_isp_out, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        printf("%s does not support "
                                 "memory mapping\n", dev_isp_out);
                        exit(EXIT_FAILURE);
                } else {
                        errno_exit("VIDIOC_REQBUFS");
                }
        }
        printf("calloc user buffer array\n");
        buffers_isp_out = (struct buffer *)calloc(req.count, sizeof(*buffers_isp_out));

        if (!buffers_isp_out) {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }

		CLEAR(buf);

		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory      = V4L2_MEMORY_MMAP;
		buf.index       = 0;

		if (-1 == xioctl(fd_isp_out, VIDIOC_QUERYBUF, &buf))
				errno_exit("VIDIOC_QUERYBUF");
		printf("Memory map element %d of length %d\n", n_buffers,  buf.length);
		buffers_isp_out->length = buf.length;
		buffers_isp_out->start =
				mmap(NULL /* start anywhere */,
					  buf.length,
					  PROT_READ | PROT_WRITE /* required */,
					  MAP_SHARED /* recommended */,
					  fd_isp_out, buf.m.offset);

		if (MAP_FAILED == buffers_isp_out->start)
				errno_exit("mmap");

}
static void init_device(void)
{
        struct v4l2_capability cap;
        struct v4l2_cropcap cropcap;
        struct v4l2_crop crop;
        struct v4l2_format fmt;
        unsigned int min;
        printf("VIDIOC_QUERYCAP \n");
        if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
                if (EINVAL == errno) {
                        fprintf(stderr, "%s is no V4L2 device\n",
                                 dev_name);
                        exit(EXIT_FAILURE);
                } else {
                        errno_exit("VIDIOC_QUERYCAP");
                }
        }

        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
                fprintf(stderr, "%s is no video capture device\n",
                         dev_name);
                exit(EXIT_FAILURE);
        }
        printf("Can perform Video Capture \n");
        switch (io) {
        case IO_METHOD_READ:
                if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
                        fprintf(stderr, "%s does not support read i/o\n",
                                 dev_name);
                        exit(EXIT_FAILURE);
                }
                break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
                if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
                        fprintf(stderr, "%s does not support streaming i/o\n",
                                 dev_name);
                        exit(EXIT_FAILURE);
                }
                break;
        }


        /* Select video input, video standard and tune here. */


        CLEAR(cropcap);

        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
                crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                crop.c = cropcap.defrect; /* reset to default */
                printf("Setting Picture crop to %d x %d",cropcap.defrect.width ,cropcap.defrect.height);

                if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
                        switch (errno) {
                        case EINVAL:
                                /* Cropping not supported. */
                                break;
                        default:
                                /* Errors ignored. */
                                break;
                        }
                }
        } else {
                /* Errors ignored. */
        }


        CLEAR(fmt);

        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (force_format) {
        		printf("Set H264\r\n");
                fmt.fmt.pix.width       = 1920; //replace
                fmt.fmt.pix.height      = 1080; //replace
                fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SRGB10; //replace
                fmt.fmt.pix.field       = V4L2_FIELD_ANY;

                if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
                        errno_exit("VIDIOC_S_FMT");

                /* Note VIDIOC_S_FMT may change width and height. */
        } else {
                /* Preserve original settings as set by v4l2-ctl for example */
                if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
                        errno_exit("VIDIOC_G_FMT");
        }

        /* Buggy driver paranoia. */
        min = fmt.fmt.pix.width * 2;
        if (fmt.fmt.pix.bytesperline < min)
                fmt.fmt.pix.bytesperline = min;
        min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
        if (fmt.fmt.pix.sizeimage < min)
                fmt.fmt.pix.sizeimage = min;

        switch (io) {
        case IO_METHOD_READ:
                init_read(fmt.fmt.pix.sizeimage);
                break;

        case IO_METHOD_MMAP:
                init_mmap();
                break;

        case IO_METHOD_USERPTR:
                init_userp(fmt.fmt.pix.sizeimage);
                break;
        }
}

static void close_device(void)
{
        if (-1 == close(fd))
                errno_exit("close");

        fd = -1;
}

static void open_device(void)
{
        struct stat st;

        if (-1 == stat(dev_name, &st)) {
                fprintf(stderr, "Cannot identify '%s': %d, %s\n",
                         dev_name, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (!S_ISCHR(st.st_mode)) {
                fprintf(stderr, "%s is no device\n", dev_name);
                exit(EXIT_FAILURE);
        }

        fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

        if (-1 == fd) {
                fprintf(stderr, "Cannot open '%s': %d, %s\n",
                         dev_name, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }
}

static void open_isp(void)
{
        /* Open ISP Input */
        struct stat st;

        if (-1 == stat(dev_isp_in, &st)) {
                printf("Cannot identify '%s': %d, %s\n",
                         dev_isp_in, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (!S_ISCHR(st.st_mode)) {
                printf("%s is no device\n", dev_isp_in);
                exit(EXIT_FAILURE);
        }

        fd_isp_in = open(dev_isp_in, O_RDWR /* required */ | O_NONBLOCK, 0);

        if (-1 == fd_isp_in) {
                printf("Cannot open '%s': %d, %s\n",
                         dev_isp_in, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }       

        /* Open ISP Output */

        if (-1 == stat(dev_isp_out, &st)) {
                printf("Cannot identify '%s': %d, %s\n",
                         dev_isp_out, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (!S_ISCHR(st.st_mode)) {
                printf("%s is no device\n", dev_isp_out);
                exit(EXIT_FAILURE);
        }

        fd_isp_out = open(dev_isp_out, O_RDWR /* required */ | O_NONBLOCK, 0);

        if (-1 == fd_isp_out) {
                printf("Cannot open '%s': %d, %s\n",
                         dev_isp_out, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }

}

static void usage(FILE *fp, int argc, char **argv)
{
        fprintf(fp,
                 "Usage: %s [options]\n\n"
                 "Version 1.3\n"
                 "Options:\n"
                 "-d | --device name   Video device name [%s]\n"
                 "-h | --help          Print this message\n"
                 "-m | --mmap          Use memory mapped buffers [default]\n"
                 "-r | --read          Use read() calls\n"
                 "-u | --userp         Use application allocated buffers\n"
                 "-o | --output        Outputs stream to stdout\n"
                 "-f | --format        Force format to 640x480 YUYV\n"
                 "-c | --count         Number of frames to grab [%i]\n"
                 "",
                 argv[0], dev_name, frame_count);
}

static const char short_options[] = "d:hmruofc:";

static const struct option
long_options[] = {
        { "device", required_argument, NULL, 'd' },
        { "help",   no_argument,       NULL, 'h' },
        { "mmap",   no_argument,       NULL, 'm' },
        { "read",   no_argument,       NULL, 'r' },
        { "userp",  no_argument,       NULL, 'u' },
        { "output", no_argument,       NULL, 'o' },
        { "format", no_argument,       NULL, 'f' },
        { "count",  required_argument, NULL, 'c' },
        { 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
        dev_name = "/dev/video0";
        dev_isp_in = "/dev/video13";
        dev_isp_out = "/dev/video14";

        for (;;) {
                int idx;
                int c;

                c = getopt_long(argc, argv,
                                short_options, long_options, &idx);

                if (-1 == c)
                        break;

                switch (c) {
                case 0: /* getopt_long() flag */
                        break;

                case 'd':
                        dev_name = optarg;
                        break;

                case 'h':
                        usage(stdout, argc, argv);
                        exit(EXIT_SUCCESS);

                case 'm':
                        io = IO_METHOD_MMAP;
                        break;

                case 'r':
                        io = IO_METHOD_READ;
                        break;

                case 'u':
                        io = IO_METHOD_USERPTR;
                        break;

                case 'o':
                        out_buf++;
                        break;

                case 'f':
                        force_format++;
                        break;

                case 'c':
                        errno = 0;
                        frame_count = strtol(optarg, NULL, 0);
                        if (errno)
                                errno_exit(optarg);
                        break;

                default:
                        usage(stderr, argc, argv);
                        exit(EXIT_FAILURE);
                }
        }
        int result;

        result = system(mediaSetup);
        result = system(v4l2Setup);
        result = system(ispInSetup);
        result = system(ispOutSetup);        
        sprintf(mediaSetup,"rm frame-*");
        open_device();
        open_isp();
        init_device();
        init_mmap_isp();
        start_capturing();
        mainloop();
        stop_capturing();
        uninit_device();
        close_device();
        fprintf(stderr, "\n");
        return 0;
}
