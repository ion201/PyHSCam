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

// Disable security warnings because accoring to microsoft, snprintf "may be unsafe"...
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <limits.h>

#include <boost/python.hpp>
#include <exception>
#include <string>
#include <sstream>

#include "PDCLIB.h"

// Max buffer length for exception messages
#define MSG_BUF_MAX_LEN 80


// Function prototypes
void
    PyHSCam_init(void);

uint64_t
    PyHSCam_openDeviceByIp(const char * ipStr);

boost::python::list
    PyHSCam_getValidCapRates(uint64_t interfaceId);

void
    PyHSCam_setCapRate(uint64_t interfaceId, unsigned long capRate);

boost::python::tuple
    PyHSCam_getCurrentResolution(uint64_t interfaceId);

void
    PyHSCam_setResolution(uint64_t interfaceId, unsigned long width, unsigned long height);

PyObject *  // Py_Bytes
    PyHSCam_captureImage(uint64_t interfaceId);

boost::python::list
    PyHSCam_getAllValidResolutions(uint64_t interfaceId);

bool
    PyHSCam_isDeviceMonochromatic(uint64_t interfaceId);

void
    PyHSCam_recordFrames(uint64_t interfaceId, uint32_t nFrames);


// Combine deviceNum and childNum into a single uint64_t
#define IFACE_ID_FIELD_OFFSET 32
#define IFACE_ID_FROM_VALUES(deviceNum, childNum) ((((uint64_t)deviceNum) << IFACE_ID_FIELD_OFFSET) + childNum)
#define IFACE_ID_GET_DEV_NUM(interfaceId) ((unsigned long)(interfaceId >> IFACE_ID_FIELD_OFFSET))
#define IFACE_ID_GET_CHILD_NUM(interfaceId) ((unsigned long)(interfaceId & (((uint64_t)1 << IFACE_ID_FIELD_OFFSET) - 1)))


// Implement a custom python exception so that we don't have to raise RuntimeError
// The python code will be able to catch PyCamHS.CamRuntimeError
class CamRuntimeError : public std::exception
{
private:
    std::string message;
    unsigned long errorCode = ULONG_MAX;
public:
    CamRuntimeError(std::string message, unsigned long errorCode)
    {
        this->message = message;
        this->errorCode = errorCode;
    }
    CamRuntimeError(std::string message)
    {
        this->message = message;
    }
    const char * what() const throw()
    {
        return this->message.c_str();
    }
    ~CamRuntimeError() throw()
    {
    }
    std::string getMessage() const
    {
        return this->message;
    }
    unsigned long getErrorCode() const
    {
        return this->errorCode;
    }
    bool hasErrorCode() const
    {
        if (this->errorCode == ULONG_MAX)
        {
            return false;
        }
        return true;
    }
};
PyObject * createExceptionClass(const char * name, PyObject * baseTypeObj = PyExc_Exception)
{
    // Create an exception that will be exposed in python as ModuleName.name
    std::string scopeName = boost::python::extract<std::string>(boost::python::scope().attr("__name__"));
    std::string qualifiedName0 = scopeName + "." + name;
    char * qualifiedName1 = const_cast<char *>(qualifiedName0.c_str());

    PyObject * typeObj = PyErr_NewException(qualifiedName1, baseTypeObj, NULL);
    if (!typeObj)
    {
        boost::python::throw_error_already_set();
    }
    boost::python::scope().attr(name) = boost::python::handle<>(boost::python::borrowed(typeObj));
    return typeObj;
}
PyObject * pyCamRuntimeError = NULL;
void convertCppExceptionToPy(CamRuntimeError const &except)
{
    // Converts the c++ exception class CamRuntimeError into the python exception class created
    // by createExceptionClass() and assigns a formatted string for it's message.
    if (pyCamRuntimeError == NULL)
    {
        throw std::runtime_error("Module attempted to raise exception before it was initialized."
                                 "This should never happen.");
    }
    char msgBuf[MSG_BUF_MAX_LEN];
    if (except.hasErrorCode())
    {
        snprintf(&msgBuf[0],
                    MSG_BUF_MAX_LEN,
                    "%s\n"
                    "Error Code: %d",
                    except.getMessage().c_str(),
                    except.getErrorCode());
        PyObject_SetAttrString(pyCamRuntimeError,
                        "errorCode",
                        PyLong_FromUnsignedLong(except.getErrorCode()));
    }
    else
    {
        snprintf(&msgBuf[0],
            MSG_BUF_MAX_LEN,
            "%s",
            except.getMessage().c_str());
    }
    PyErr_SetString(pyCamRuntimeError, msgBuf);
}


void PyHSCam_init(void)
{
    // Add a search directory for dlls
    if (0 == SetDllDirectory("dll/"))
    {
        throw CamRuntimeError("SetDllDirectory failed. This error should never happen.");
    }

    unsigned long retVal;
    unsigned long errorCode;

    retVal = PDC_Init(&errorCode);
    if (retVal == PDC_FAILED)
    {
        throw CamRuntimeError("Init failed!", errorCode);
    }
}


uint64_t PyHSCam_openDeviceByIp(const char * ipStr)
{
    unsigned long retVal;
    unsigned long errorCode;

    // Convert ip string to 32 bit int
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
                                &detectedNumInfo,   // Output
                                &errorCode);        // Output

    if (retVal == PDC_FAILED)
    {
        throw CamRuntimeError("Device search failed!", errorCode);
    }
    if (detectedNumInfo.m_nDeviceNum == 0)
    {
        throw CamRuntimeError("SDK reported 0 devices found!");
    }
    if (detectedNumInfo.m_DetectInfo[0].m_nTmpDeviceNo != ipList[0])
    {
        char msgBuf[40];
        snprintf(&msgBuf[0],
            40,
            "SDK found unexpected ip = %#08x\n",
            detectedNumInfo.m_DetectInfo[0].m_nTmpDeviceNo);
        throw CamRuntimeError(msgBuf);
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
        throw CamRuntimeError("Failed to open device!", errorCode);
    }

    childNum = 1; // TODO: support more than one child

    // TODO: Return a list of IDs (required for supporting more than one child).

    return IFACE_ID_FROM_VALUES(deviceNum, childNum);
}

boost::python::list PyHSCam_getValidCapRates(uint64_t interfaceId)
{
    unsigned long retVal;
    unsigned long modeCnt;
    unsigned long modeList[PDC_MAX_LIST_NUMBER];
    unsigned long errorCode;

    retVal = PDC_GetRecordRateList(IFACE_ID_GET_DEV_NUM(interfaceId),
                                    IFACE_ID_GET_CHILD_NUM(interfaceId),
                                    &modeCnt,       // Output
                                    modeList,       // Output
                                    &errorCode);    // Output
    if (retVal == PDC_FAILED)
    {
        throw CamRuntimeError("Failed to retrieve rate list!", errorCode);
    }

    boost::python::list pyModeList;

    unsigned long i;
    for (i = 0; i < modeCnt; i++)
    {
        pyModeList.append(modeList[i]);
    }

    return pyModeList;
}

void PyHSCam_setCapRate(uint64_t interfaceId, unsigned long capRate)
{
    boost::python::list pyModeList;
    pyModeList = PyHSCam_getValidCapRates(interfaceId);

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
        throw CamRuntimeError("The requested capture rate is not valid!");
    }

    unsigned long retVal;
    unsigned long errorCode;
    retVal = PDC_SetRecordRate(IFACE_ID_GET_DEV_NUM(interfaceId),
                                IFACE_ID_GET_CHILD_NUM(interfaceId),
                                capRate ,
                                &errorCode);  // Output
    if (retVal == PDC_FAILED)
    {
        throw CamRuntimeError("Setting new record rate failed!", errorCode);
    }
}

PyObject * PyHSCam_captureImage(uint64_t interfaceId)
{
    char * imageBuf;
    unsigned long imgWidth;
    unsigned long imgHeight;
    unsigned long errorCode;
    unsigned long retVal;

    boost::python::tuple imgResolution = PyHSCam_getCurrentResolution(interfaceId);
    imgWidth = boost::python::extract<unsigned long>(imgResolution[0]);
    imgHeight = boost::python::extract<unsigned long>(imgResolution[1]);

    uint32_t imgBufSize;

    // TODO: Add support for 16-bit color images
    if (PyHSCam_isDeviceMonochromatic(interfaceId))
    {
        imgBufSize = imgWidth * imgHeight;
    }
    else  // RGB Color
    {
        imgBufSize = imgWidth * imgHeight * 3;
    }
    imageBuf = (char *)malloc(imgBufSize);

    retVal = PDC_GetLiveImageData(IFACE_ID_GET_DEV_NUM(interfaceId),
                                    IFACE_ID_GET_CHILD_NUM(interfaceId),
                                    8,  // Assume 8-bit color depth
                                    imageBuf,
                                    &errorCode);

    PyObject * pyImageBuf = PyBytes_FromStringAndSize(imageBuf, imgBufSize);
    free(imageBuf);

    return pyImageBuf;
}

boost::python::tuple PyHSCam_getCurrentResolution(uint64_t interfaceId)
{
    unsigned long width;
    unsigned long height;
    unsigned long errorCode;
    unsigned long retVal;

    retVal = PDC_GetResolution(IFACE_ID_GET_DEV_NUM(interfaceId),
                                IFACE_ID_GET_CHILD_NUM(interfaceId),
                                &width,         // Output
                                &height,        // Output
                                &errorCode);    // Output

    if (retVal == PDC_FAILED)
    {
        throw CamRuntimeError("Failed to read current resolution!", errorCode);
    }

    return boost::python::make_tuple(width, height);;
}

void PyHSCam_setResolution(uint64_t interfaceId, unsigned long width, unsigned long height)
{
    unsigned long retVal;
    unsigned long errorCode;

    retVal = PDC_SetResolution(IFACE_ID_GET_DEV_NUM(interfaceId),
                                IFACE_ID_GET_CHILD_NUM(interfaceId),
                                width,
                                height,
                                &errorCode);    // Output

    if (retVal == PDC_FAILED)
    {
        throw CamRuntimeError("Failed to set resolution!", errorCode);
    }
}

boost::python::list PyHSCam_getAllValidResolutions(uint64_t interfaceId)
{
    unsigned long retVal;
    unsigned long errorCode;

    unsigned long resList[PDC_MAX_LIST_NUMBER];
    unsigned long resListSize;

    retVal = PDC_GetResolutionList(IFACE_ID_GET_DEV_NUM(interfaceId),
                                    IFACE_ID_GET_CHILD_NUM(interfaceId),
                                    &resListSize,       // Output
                                    &resList[0],        // Output
                                    &errorCode);        // Output

    if (retVal == PDC_FAILED)
    {
        throw CamRuntimeError("Failed to retrieve list of valid resolutions!", errorCode);
    }

    boost::python::list pyResList;

    unsigned long i;
    for (i = 0; i < resListSize; i++)
    {
        unsigned long width  = (resList[i] & 0xffff0000) >> 16;
        unsigned long height = (resList[i] & 0x0000ffff);
        pyResList.append(boost::python::make_tuple(width, height));
    }

    return pyResList;
}

bool PyHSCam_isDeviceMonochromatic(uint64_t interfaceId)
{
    unsigned long retVal;
    unsigned long errorCode;
    char colorMode;

    retVal = PDC_GetColorType(IFACE_ID_GET_DEV_NUM(interfaceId),
                                IFACE_ID_GET_CHILD_NUM(interfaceId),
                                &colorMode,         // Output
                                &errorCode);        // Output
    if (retVal == PDC_FAILED)
    {
        throw CamRuntimeError("Failed to retrieve device color mode!", errorCode);
    }

    if (colorMode == PDC_COLORTYPE_MONO)
    {
        return true;
    }
    return false;
}


void PyHSCam_recordFrames(uint64_t interfaceId, uint32_t nFrames)
{
    unsigned long retVal;
    unsigned long errorCode;

    // retVal = PDC_SetTriggerMode(IFACE_ID_GET_DEV_NUM(interfaceId),
    //                             PDC_TRIGGER_MANUAL,
    //                             nFrames,
    //                             0,              // Unused
    //                             0,              // Unused
    //                             &errorCode);    // Output
    // if (retVal == PDC_FAILED)
    // {
    //     throw CamRuntimeError("Failed to set trigger mode!", errorCode);
    // }

    retVal = PDC_SetRecReady(IFACE_ID_GET_DEV_NUM(interfaceId),
                                &errorCode);     // Output

    if (retVal == PDC_FAILED)
    {
        throw CamRuntimeError("Failed to set record to ready!", errorCode);
    }

    // This should confirm the operating mode of the device
    unsigned long deviceStatus;
    // TODO: Should probably have a more reliable timeout
    uint32_t i;
#define STATUS_CHECK_TIMEOUT 2000
    for (i = 0; i < STATUS_CHECK_TIMEOUT; i++)
    {
        retVal = PDC_GetStatus(IFACE_ID_GET_DEV_NUM(interfaceId),
                                &deviceStatus,      // Output
                                &errorCode);        // Output
        if (retVal == PDC_FAILED)
        {
            throw CamRuntimeError("Device status check failed!", errorCode);
        }
        if ((deviceStatus == PDC_STATUS_RECREADY) ||
            (deviceStatus == PDC_STATUS_REC))
        {
            // The device is ready for recording
            break;
        }
    }
    if (i == STATUS_CHECK_TIMEOUT)
    {
        throw CamRuntimeError("Function timed out while waiting for device to enter record-ready state.");
    }

    // Start the recording
    retVal = PDC_TriggerIn(IFACE_ID_GET_DEV_NUM(interfaceId),
                            &errorCode);
    if (retVal == PDC_FAILED)
    {
        throw CamRuntimeError("Device trigger in (begin recording) failed!", errorCode);
    }

    // Wait for recording to finish
    // TODO: Should have a timeout. We can guess duration with capture rate and nFrames.
    for (i = 0; i < 20000; i++)
    {
        retVal = PDC_GetStatus(IFACE_ID_GET_DEV_NUM(interfaceId),
                                &deviceStatus,
                                &errorCode);
        if (retVal == PDC_FAILED)
        {
            throw CamRuntimeError("Error while retrieving device status!", errorCode);
        }
        if ((deviceStatus != PDC_STATUS_RECREADY) &&
            (deviceStatus != PDC_STATUS_REC))
        {
            // Recording has finished.
            break;
        }
    }
}


BOOST_PYTHON_MODULE(PyHSCam)
{
    // Set formatting for documentation
    boost::python::docstring_options docOptions;
    docOptions.disable_cpp_signatures();

    // Setup our custom exception
    pyCamRuntimeError = createExceptionClass("CamRuntimeError");
    boost::python::register_exception_translator<CamRuntimeError>(&convertCppExceptionToPy);

    // Create member functions
    boost::python::def("init",
                        PyHSCam_init,
                        "Initialize the api. This must be run before any other commands.");
    boost::python::def("openDeviceByIp",
                        PyHSCam_openDeviceByIp,
                        boost::python::args("targetIp"),
                        "Opens device at the specified ip address.\n"
                        "Returns interfaceId representing the opened device.");
    boost::python::def("setCapRate",
                        PyHSCam_setCapRate,
                        boost::python::args("interfaceId", "captureRate"),
                        "Attempts to set specified device to a particular capture rate.");
    boost::python::def("getValidCapRates",
                        PyHSCam_getValidCapRates,
                        boost::python::args("interfaceId"),
                        "Get a list of all valid capture rates for the specified device.");
    boost::python::def("captureLiveImage",
                        PyHSCam_captureImage,
                        boost::python::args("interfaceId"),
                        "Captures an image and returns the data in a bytes object.\n"
                        "By default, color images are in the interleave format (BGRBGR...)");
    boost::python::def("getCurrentResolution",
                        PyHSCam_getCurrentResolution,
                        boost::python::args("interfaceId"),
                        "Returns the current image resolution for interfaceId as a tuple of (width, height)");
    boost::python::def("setResolution",
                        PyHSCam_setResolution,
                        boost::python::args("interfaceId", "width", "height"),
                        "Sets the current resolution for interfaceId to (width, height)");
    boost::python::def("getAllValidResolutions",
                        PyHSCam_getAllValidResolutions,
                        boost::python::args("interfaceId"),
                        "Retrieves a list of valid resolutions for interfaceId.\n"
                        "Each element is a tuple containing (width, height)");
    boost::python::def("recordFrames",
                        PyHSCam_recordFrames,
                        boost::python::args("interfaceId", "nFrames"),
                        "Capture nFrames frames on interfaceId and store images to internal memory.\n");
}
