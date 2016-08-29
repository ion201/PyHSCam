/*
Copyright (c) 2016 Nate Simon (github.com: @ion201)

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify,
merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/*
This library relies on the non-free SDK owned and distributed by
Photron. It can be retrieved from:
	http://photron.com/support/downloads/
*/

// Disable security warnings because accoring to microsoft, snprintf "may be unsafe"...
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>
#include <inttypes.h>

#include <boost/python.hpp>
#include <exception>
#include <string>
#include <sstream>
#include <map>

#include "PDCLIB.h"

// Max buffer length for exception messages
#define MSG_BUF_MAX_LEN 80


// Function prototypes
void
    PyHSCam_init(void);

uint64_t
    PyHSCam_openDevice(const char * ipStr);

boost::python::list
    PyHSCam_getAllModes(uint64_t interfaceId);

void
    PyHSCam_setMode(unsigned long capRate, uint64_t interfaceId);

PyObject *
    PyHSCam_getImage(uint64_t interfaceId);


// Combine deviceNum and childNum into a single uint64_t
#define IFACE_ID_FIELD_OFFSET 32
#define IFACE_ID_FROM_VALUES(deviceNum, childNum) ((((uint64_t)deviceNum) << IFACE_ID_FIELD_OFFSET) + childNum)
#define IFACE_ID_GET_DEV_NUM(interfaceId) ((unsigned long)(interfaceId >> IFACE_ID_FIELD_OFFSET))
#define IFACE_ID_GET_CHILD_NUM(interfaceId) ((unsigned long)(interfaceId & (((uint64_t)1 << IFACE_ID_FIELD_OFFSET) - 1)))


void PyHSCam_init(void)
{
    char msgBuf[MSG_BUF_MAX_LEN];
    unsigned long retVal;
    unsigned long errorCode;

    retVal = PDC_Init(&errorCode);
    if (retVal == PDC_FAILED)
    {
        snprintf(&msgBuf[0],
                    sizeof(msgBuf)/sizeof(msgBuf[0]),
                    "Init Failed with error code %d\n",
                    errorCode);
        throw std::runtime_error(msgBuf);
    }
}


uint64_t PyHSCam_openDevice(const char * ipStr)
{
    // Initialize the SDK and open the camera at the requested ip
    // On success, returns interfaceId to uniquely identify an interface

    char msgBuf[MSG_BUF_MAX_LEN];  // For exception messages
    unsigned long retVal;
    unsigned long errorCode;

    // Need to convert ip string to 32 bit int
    // Eg: "192.168.1.10" -> 0xc0a8000a
    unsigned long ipNumeric = 0;
    std::stringstream ss(ipStr);
    std::string item;
    while (getline(ss, item, '.'))
    {
        ipNumeric = (ipNumeric << 8) | std::stoi(item);
    }

    PDC_DETECT_NUM_INFO detectedNumInfo; // Stores result
    unsigned long ipList[PDC_MAX_DEVICE];
    ipList[0] = ipNumeric;


    // Detect the attached device
    retVal = PDC_DetectDevice(PDC_INTTYPE_G_ETHER,  // Gigabit-ethernet interface
                                ipList,
                                1,                  // Max number of search devices
                                PDC_DETECT_NORMAL,  // Indicate we're specifying an ip explicitly
                                &detectedNumInfo,     // Output
                                &errorCode);        // Output

    if (retVal == PDC_FAILED)
    {
        snprintf(&msgBuf[0],
                    sizeof(msgBuf)/sizeof(msgBuf[0]),
                    "Device search failed with error code %d\n",
                    errorCode);
        throw std::runtime_error(msgBuf);
    }
    if (detectedNumInfo.m_nDeviceNum == 0)
    {
        snprintf(&msgBuf[0],
                    sizeof(msgBuf)/sizeof(msgBuf[0]),
                    "SDK reported %d devices found!\n",
                    detectedNumInfo.m_nDeviceNum);
        throw std::runtime_error(msgBuf);
    }
    if (detectedNumInfo.m_DetectInfo[0].m_nTmpDeviceNo != ipList[0])
    {
        snprintf(&msgBuf[0],
            sizeof(msgBuf)/sizeof(msgBuf[0]),
            "SDK found unexpected ip = %#08x\n",
            detectedNumInfo.m_DetectInfo[0].m_nTmpDeviceNo);
        throw std::runtime_error(msgBuf);
    }
    //TODO: Should we specify the model to validate the camera?
    // eg. Fastcam 4 : (dectNumInfo.m_DetectInfo[0].m_nDeviceCode == PDC_DEVTYPE_FCAM_SA4)

    // No errors - open the located device
    // TODO: Expect only one device at a at a time. This init function will
    // need to be rearranged if we want to expect >1 device open.
    // Should also account for devices with >1 child devices
    // (devices which have multiple camera heads).
    unsigned long deviceNum;
    unsigned long childNum;
    retVal = PDC_OpenDevice(&(detectedNumInfo.m_DetectInfo[0]),
                            &deviceNum,     // Output
                            &errorCode);    // Output
    if (retVal == PDC_FAILED)
    {
        snprintf(&msgBuf[0],
                    sizeof(msgBuf)/sizeof(msgBuf[0]),
                    "Failed to open device; error code %d\n",
                    errorCode);
        throw std::runtime_error(msgBuf);
    }

    childNum = 1; // TODO: support more than one child

    // TODO: Return a list of IDs (required for supporting more than one child).

    return IFACE_ID_FROM_VALUES(deviceNum, childNum);
}

boost::python::list PyHSCam_getAllModes(uint64_t interfaceId)
{
    unsigned long retVal;
    unsigned long modeCnt;
    unsigned long modeList[PDC_MAX_LIST_NUMBER];
    unsigned long errorCode;
    char msgBuf[MSG_BUF_MAX_LEN];

    retVal = PDC_GetRecordRateList(IFACE_ID_GET_DEV_NUM(interfaceId),
                                    IFACE_ID_GET_CHILD_NUM(interfaceId),
                                    &modeCnt,      // Output
                                    modeList,       // Output
                                    &errorCode);    // Output
    if (retVal == PDC_FAILED)
    {
        snprintf(&msgBuf[0],
                    sizeof(msgBuf)/sizeof(msgBuf[0]),
                    "Failed to retrieve rate list; error code %d\n",
                    errorCode);
        throw std::runtime_error(msgBuf);
    }

    boost::python::list pyModeList;

    unsigned long i;
    for (i = 0; i < modeCnt; i++)
    {
        pyModeList.append(modeList[i]);
    }

    return pyModeList;
}

void PyHSCam_setMode(unsigned long capRate, uint64_t interfaceId)
{
    char msgBuf[MSG_BUF_MAX_LEN];

    boost::python::list pyModeList;
    pyModeList = PyHSCam_getAllModes(IFACE_ID_GET_DEV_NUM(interfaceId));

    long modeCnt = boost::python::len(pyModeList);
    long i;
    unsigned long mode;
    for (i = 0; i < modeCnt; i++)
    {
        mode = boost::python::extract<unsigned long>(pyModeList[i]);
        if (mode == capRate)
        {
            break;
        }
    }
    if (i == modeCnt)
    {
        snprintf(&msgBuf[0],
                    sizeof(msgBuf)/sizeof(msgBuf[0]),
                    "Specified capture rate (%d) is not valid - see .getModes().\n",
                    capRate);
        throw std::runtime_error(msgBuf);
    }

    unsigned long retVal;
    unsigned long errorCode;
    retVal = PDC_SetRecordRate(IFACE_ID_GET_DEV_NUM(interfaceId),
                                IFACE_ID_GET_CHILD_NUM(interfaceId),
                                capRate,
                                &errorCode);  // Output
    if (retVal == PDC_FAILED)
    {
        snprintf(&msgBuf[0],
                    sizeof(msgBuf)/sizeof(msgBuf[0]),
                    "Specified capture rate (%d) is not valid.\n",
                    capRate);
        throw std::runtime_error(msgBuf);
    }
}

PyObject * PyHSCam_getImage(uint64_t interfaceId)
{
    // TODO: This is test code and needs to be replaced with
    // return of actual images.
    char imageBuf[50];
    int i;
    for (i = 0; i < 50; i++)
    {
        imageBuf[i] = i*6;
    }

    PyObject * pyImageBuf = PyBytes_FromStringAndSize(&imageBuf[0], 50);

    return pyImageBuf;
}


BOOST_PYTHON_MODULE(PyHSCam)
{
    // Setup module members and documentation
    boost::python::docstring_options docOptions;
    docOptions.disable_cpp_signatures();

    boost::python::def("init",
                        PyHSCam_init,
                        "Initialize the api. This must be run before any other commands.");
    boost::python::def("openDevice",
                        PyHSCam_openDevice,
                        boost::python::args("targetIp"),
                        "Opens device at the specified ip address.\n"
                        "Returns interfaceId representing the opened device.");
    boost::python::def("setMode",
                        PyHSCam_setMode,
                        boost::python::args("captureRate", "interfaceId"),
                        "Attempts to set specified device to a particular capture rate.");
    boost::python::def("getModes",
                        PyHSCam_getAllModes,
                        boost::python::args("interfaceId"),
                        "Get a list of all valid capture rates for the specified device.");
    boost::python::def("getImage",
                        PyHSCam_getImage,
                        boost::python::args("interfaceId"),
                        "Captures an image and returns the data in a bytes object.");
}
