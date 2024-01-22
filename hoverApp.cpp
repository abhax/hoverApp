
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "videoStream.h"


int main(int argc, char **argv)
{
       videoStream_init();
       videoStream_startCapture();
       videoStream_stopCapture();
       videoStream_close();
       return 0;
}
