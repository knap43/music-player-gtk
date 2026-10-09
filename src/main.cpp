#include "Application.h"

#include <gst/gst.h>

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    auto app = Application::create();
    const int status = app->run(argc, argv);
    gst_deinit();
    return status;
}
