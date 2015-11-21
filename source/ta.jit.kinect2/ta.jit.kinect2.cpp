/**
	 @file
    ta.jit.kinect2 - reads rgb and depth matrix from Kinect v2
    using libfreenect2
 
    @ingroup	examples
	@see		ta.jit.kinect2
 
	Copyright 2015 - Tiago Ângelo aka p1nh0 (p1nh0.c0d1ng@gmail.com)*/

#include "jit.common.h"

// Libfreenect2 includes
#include <iostream>
#include <signal.h>
#include <libfreenect2.hpp>
#include <frame_listener_impl.h>
#include <registration.h>
#include <packet_pipeline.h>
//#include <logger.h>


// Our Jitter object instance data
typedef struct _ta_jit_kinect2 {
	t_object	ob;
    long		depth_processor;	// TA: depth_processor attribute -> 0=CPU, 1=OpenGL, 2=OpenCL
    libfreenect2::Freenect2 *freenect2;
//    std::string *serial;
    libfreenect2::Freenect2Device *device; // TA: declare freenect2 device
    libfreenect2::PacketPipeline *pipeline; // TA: declare packet pipeline
    
    libfreenect2::SyncMultiFrameListener listener; //TA: frame listener
    libfreenect2::FrameMap *frames; // TA: frame map
    libfreenect2::Frame *undistorted, *registered;
    libfreenect2::Registration* registration;
    size_t framecount;
} t_ta_jit_kinect2;


// prototypes
BEGIN_USING_C_LINKAGE
t_jit_err		ta_jit_kinect2_init				(void);
t_ta_jit_kinect2	*ta_jit_kinect2_new				(void);
void			ta_jit_kinect2_free				(t_ta_jit_kinect2 *x);
t_jit_err		ta_jit_kinect2_matrix_calc		(t_ta_jit_kinect2 *x, void *inputs, void *outputs);
void ta_jit_kinect2_open (t_ta_jit_kinect2 *x); // TA: declare "open" method
// TA: ta.jit.kinect2 doesn't make any ndim calculation (there are no pixels being changed!)
//void			ta_jit_kinect2_calculate_ndim	(t_ta_jit_kinect2 *x, long dim, long *dimsize, long planecount, t_jit_matrix_info *in_minfo, char *bip, t_jit_matrix_info *out_minfo, char *bop);
END_USING_C_LINKAGE


// globals TA: GLOBAL CLASS VARIABLE (class information resides here)
static void *s_ta_jit_kinect2_class = NULL;
//std::string serial; //TA: not needed ?

/************************************************************************************/
// INIT
t_jit_err ta_jit_kinect2_init(void)
{
	long			attrflags = JIT_ATTR_GET_DEFER_LOW | JIT_ATTR_SET_USURP_LOW;
	t_jit_object	*attr;
	t_jit_object	*mop;

	s_ta_jit_kinect2_class = jit_class_new("ta_jit_kinect2", (method)ta_jit_kinect2_new, (method)ta_jit_kinect2_free, sizeof(t_ta_jit_kinect2), 0);

	// add matrix operator (mop)
	mop = (t_jit_object *)jit_object_new(_jit_sym_jit_mop, 1, 2); // args are  num inputs and num outputs | TA: added a second outlet, thus having depth, rgb and dumpout outlets
	jit_class_addadornment(s_ta_jit_kinect2_class, mop);

	// add method(s)
	jit_class_addmethod(s_ta_jit_kinect2_class, (method)ta_jit_kinect2_matrix_calc, "matrix_calc", A_CANT, 0);
    jit_class_addmethod(s_ta_jit_kinect2_class, (method)ta_jit_kinect2_open, "open", 0);
    
	// add attribute(s)
	attr = (t_jit_object *)jit_object_new(_jit_sym_jit_attr_offset,
										  "depth_processor",
										  _jit_sym_long,
										  attrflags,
										  (method)NULL, (method)NULL,
										  calcoffset(t_ta_jit_kinect2, depth_processor));
	jit_class_addattr(s_ta_jit_kinect2_class, attr);

	// finalize class (REGISTER)
	jit_class_register(s_ta_jit_kinect2_class);
	return JIT_ERR_NONE;
}


/************************************************************************************/
// Object Life Cycle : CONSTRUCTOR

t_ta_jit_kinect2 *ta_jit_kinect2_new(void)
{
	t_ta_jit_kinect2	*x = NULL;
    // TA: ALLOCATE OBJECT STRUCT (only initializes object struct)
	x = (t_ta_jit_kinect2 *)jit_object_alloc(s_ta_jit_kinect2_class);
    // TA: initialize other data or structs
    if (x) {
		x->depth_processor = 2; //TA: default depth-processor is OpenCL
        x->freenect2 = new libfreenect2::Freenect2();
        x->device = 0; //TA: init device
        x->pipeline = 0; //TA: init pipeline
//        serial.assign("005867245247");
	}
	return x;
}

// DESTRUCTOR
void ta_jit_kinect2_free(t_ta_jit_kinect2 *x)
{
	;	// TA: close kinect device and free allocated resources (except for the object struct)
}


/************************************************************************************/
// Methods bound to input/inlets

//TA: open kinect device
void ta_jit_kinect2_open(t_ta_jit_kinect2 *x){
    
    post("reaching for Kinect2 device"); // TA: insert "open" method here
//    libfreenect2::Freenect2 freenect2;
    
    // TA: check for connected devices
    if (x->freenect2->enumerateDevices() == 0) {
        post("no device connected!");
        return; // TA: exit open() method if no device is connected
    }
    post("reaching kinect devices...");
//    x->serial = x->freenect2->getDefaultDeviceSerialNumber(); //TA: this crashes the application with EXC_BAD_ACCESS i386 GFLT
    if(!x->pipeline){
        x->pipeline = new libfreenect2::OpenCLPacketPipeline();
        post("creating OpenCL packet pipeline");
    }
    if(x->pipeline){
//    x->device = x->freenect2->openDevice(serial, x->pipeline); // TA: this crashes the application with "Undefined symbols for architecture x86_64"
        x->device = x->freenect2->openDefaultDevice();
        post("opened default Kinect device");
    }
    if(x->device == 0){
        post("failed to open device...");
        return;
    }
    x->device->setColorFrameListener(&x->listener);
    x->device->setIrAndDepthFrameListener(&x->listener);
    x->device->start();
    
    x->registration = new libfreenect2::Registration(x->device->getIrCameraParams(), x->device->getColorCameraParams());
    x->framecount = 0; // TA: restart framecount
    x->device->stop();
}

t_jit_err ta_jit_kinect2_matrix_calc(t_ta_jit_kinect2 *x, void *inputs, void *outputs)
{
	t_jit_err			err = JIT_ERR_NONE;
	long				in_savelock;
	long				out_savelock;
	t_jit_matrix_info	in_minfo;
	t_jit_matrix_info	out_minfo;
	char				*in_bp;
	char				*out_bp;
	long				i;
	long				dimcount;
	long				planecount;
	long				dim[JIT_MATRIX_MAX_DIMCOUNT];
	void				*in_matrix;
	void				*out_matrix;

	in_matrix 	= jit_object_method(inputs,_jit_sym_getindex,0);
	out_matrix 	= jit_object_method(outputs,_jit_sym_getindex,0);

	if (x && in_matrix && out_matrix) {
		in_savelock = (long) jit_object_method(in_matrix, _jit_sym_lock, 1);
		out_savelock = (long) jit_object_method(out_matrix, _jit_sym_lock, 1);

		jit_object_method(in_matrix, _jit_sym_getinfo, &in_minfo);
		jit_object_method(out_matrix, _jit_sym_getinfo, &out_minfo);

		jit_object_method(in_matrix, _jit_sym_getdata, &in_bp);
		jit_object_method(out_matrix, _jit_sym_getdata, &out_bp);

		if (!in_bp) {
			err=JIT_ERR_INVALID_INPUT;
			goto out;
		}
		if (!out_bp) {
			err=JIT_ERR_INVALID_OUTPUT;
			goto out;
		}
		if (in_minfo.type != out_minfo.type) {
			err = JIT_ERR_MISMATCH_TYPE;
			goto out;
		}

		//get dimensions/planecount
		dimcount   = out_minfo.dimcount;
		planecount = out_minfo.planecount;

		for (i=0; i<dimcount; i++) {
			//if dimsize is 1, treat as infinite domain across that dimension.
			//otherwise truncate if less than the output dimsize
			dim[i] = out_minfo.dim[i];
			if ((in_minfo.dim[i]<dim[i]) && in_minfo.dim[i]>1) {
				dim[i] = in_minfo.dim[i];
			}
		}
        
// TA: ta.jit.kinect2 doesn't make any ndim calculation (there are no pixels being changed!)
//		jit_parallel_ndim_simplecalc2((method)ta_jit_kinect2_calculate_ndim,
//									  x, dimcount, dim, planecount, &in_minfo, in_bp, &out_minfo, out_bp,
//									  0 /* flags1 */, 0 /* flags2 */);
        

	}
	else
		return JIT_ERR_INVALID_PTR;

out:
	jit_object_method(out_matrix,_jit_sym_lock,out_savelock);
	jit_object_method(in_matrix,_jit_sym_lock,in_savelock);
	return err;
}


// TA: there is no need for vector operations in ta.jit.kinect2
/*
// We are using a C++ template to process a vector of the matrix for any of the given types.
// Thus, we don't need to duplicate the code for each datatype.
template<typename T>
void ta_jit_kinect2_vector(t_ta_jit_kinect2 *x, long n, t_jit_op_info *in, t_jit_op_info *out)
{
//	double	gain = x->gain;
	T		*ip;
	T		*op;
	long	is,
			os;
	long	tmp;

	ip = ((T *)in->p);
	op = ((T *)out->p);
	is = in->stride;
	os = out->stride;

	if ((is==1) && (os==1)) {
		++n;
		--op;
		--ip;
		while (--n) {
			tmp = *++ip;
//			*++op = tmp * gain;
		}
	}
	else {
		while (n--) {
			tmp = *ip;
//			*op = tmp * gain;
			ip += is;
			op += os;
		}
	}
}


// We also use a C+ template for the loop that wraps the call to jit_simple_vector(),
// further reducing code duplication in jit_simple_calculate_ndim().
// The calls into these templates should be inlined by the compiler, eliminating concern about any added function call overhead.
template<typename T>
void ta_jit_kinect2_loop(t_ta_jit_kinect2 *x, long n, t_jit_op_info *in_opinfo, t_jit_op_info *out_opinfo, t_jit_matrix_info *in_minfo, t_jit_matrix_info *out_minfo, char *bip, char *bop, long *dim, long planecount, long datasize)
{
	long	i;
	long	j;

	for (i=0; i<dim[1]; i++) {
		for (j=0; j<planecount; j++) {
			in_opinfo->p  = bip + i * in_minfo->dimstride[1]  + (j % in_minfo->planecount) * datasize;
			out_opinfo->p = bop + i * out_minfo->dimstride[1] + (j % out_minfo->planecount) * datasize;
			ta_jit_kinect2_vector<T>(x, n, in_opinfo, out_opinfo);
		}
	}
    
    
}
*/
 
// TA: ta.jit.kinect2 doesn't make any ndim calculation (there are no pixels being changed!)
/*
void ta_jit_kinect2_calculate_ndim(t_ta_jit_kinect2 *x, long dimcount, long *dim, long planecount, t_jit_matrix_info *in_minfo, char *bip, t_jit_matrix_info *out_minfo, char *bop)
{
    
	long			i;
	long			n;
	char			*ip;
	char			*op;
	t_jit_op_info	in_opinfo;
	t_jit_op_info	out_opinfo;

	if (dimcount < 1)
		return; // safety

	switch (dimcount) {
	case 1:
		dim[1]=1;
		// (fall-through to next case is intentional)
	case 2:
		// if planecount is the same then flatten planes - treat as single plane data for speed
		n = dim[0];
		if ((in_minfo->dim[0] > 1) && (out_minfo->dim[0] > 1) && (in_minfo->planecount == out_minfo->planecount)) {
			in_opinfo.stride = 1;
			out_opinfo.stride = 1;
			n *= planecount;
			planecount = 1;
		}
		else {
			in_opinfo.stride =  in_minfo->dim[0]>1  ? in_minfo->planecount  : 0;
			out_opinfo.stride = out_minfo->dim[0]>1 ? out_minfo->planecount : 0;
		}

		if (in_minfo->type == _jit_sym_char)
			ta_jit_kinect2_loop<uchar>(x, n, &in_opinfo, &out_opinfo, in_minfo, out_minfo, bip, bop, dim, planecount, 1);
		else if (in_minfo->type == _jit_sym_long)
			ta_jit_kinect2_loop<long>(x, n, &in_opinfo, &out_opinfo, in_minfo, out_minfo, bip, bop, dim, planecount, 4);
		else if (in_minfo->type == _jit_sym_float32)
			ta_jit_kinect2_loop<float>(x, n, &in_opinfo, &out_opinfo, in_minfo, out_minfo, bip, bop, dim, planecount, 4);
		else if (in_minfo->type == _jit_sym_float64)
			ta_jit_kinect2_loop<double>(x, n, &in_opinfo, &out_opinfo, in_minfo, out_minfo, bip, bop, dim, planecount, 8);
		break;
	default:
		for	(i=0; i<dim[dimcount-1]; i++) {
			ip = bip + i * in_minfo->dimstride[dimcount-1];
			op = bop + i * out_minfo->dimstride[dimcount-1];
			ta_jit_kinect2_calculate_ndim(x, dimcount-1, dim, planecount, in_minfo, ip, out_minfo, op);
		}
	}
}
*/