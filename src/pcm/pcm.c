/*
 *  PCM Interface - main file
 *  Copyright (c) 1998 by Jaroslav Kysela <perex@suse.cz>
 *  Copyright (c) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
  
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <limits.h>
#include <dlfcn.h>
#include "pcm_local.h"
#include "list.h"

snd_pcm_type_t snd_pcm_type(snd_pcm_t *pcm)
{
	assert(pcm);
	return pcm->type;
}

snd_pcm_type_t snd_pcm_stream(snd_pcm_t *pcm)
{
	assert(pcm);
	return pcm->stream;
}

int snd_pcm_close(snd_pcm_t *pcm)
{
	int ret = 0;
	int err;
	assert(pcm);
	if (pcm->setup) {
		if (pcm->mode & SND_PCM_NONBLOCK)
			snd_pcm_drop(pcm);
		else
			snd_pcm_drain(pcm);
	}
	if (pcm->mmap_channels) {
		if ((err = snd_pcm_munmap(pcm)) < 0)
			ret = err;
	}
	if ((err = pcm->ops->close(pcm->op_arg)) < 0)
		ret = err;
	pcm->setup = 0;
	if (pcm->name)
		free(pcm->name);
	free(pcm);
	return 0;
}	

int snd_pcm_nonblock(snd_pcm_t *pcm, int nonblock)
{
	int err;
	assert(pcm);
	if ((err = pcm->ops->nonblock(pcm->op_arg, nonblock)) < 0)
		return err;
	if (nonblock)
		pcm->mode |= SND_PCM_NONBLOCK;
	else
		pcm->mode &= ~SND_PCM_NONBLOCK;
	return 0;
}

int snd_pcm_async(snd_pcm_t *pcm, int sig, pid_t pid)
{
	assert(pcm);
	return pcm->ops->async(pcm->op_arg, sig, pid);
}

int snd_pcm_info(snd_pcm_t *pcm, snd_pcm_info_t *info)
{
	assert(pcm && info);
	return pcm->ops->info(pcm->op_arg, info);
}

int snd_pcm_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t *params)
{
	int err;
	assert(pcm && params);
	assert(pcm->setup);
	if ((err = pcm->ops->sw_params(pcm->op_arg, params)) < 0)
		return err;
	pcm->start_mode = params->start_mode;
	pcm->ready_mode = params->ready_mode;
	pcm->xrun_mode = params->xrun_mode;
	pcm->avail_min = params->avail_min;
	pcm->xfer_min = params->xfer_min;
	pcm->xfer_align = params->xfer_align;
	pcm->time = params->time;
	pcm->boundary = params->boundary;
	return 0;
}

int snd_pcm_status(snd_pcm_t *pcm, snd_pcm_status_t *status)
{
	assert(pcm && status);
	return pcm->fast_ops->status(pcm->fast_op_arg, status);
}

int snd_pcm_state(snd_pcm_t *pcm)
{
	assert(pcm);
	return pcm->fast_ops->state(pcm->fast_op_arg);
}

int snd_pcm_delay(snd_pcm_t *pcm, ssize_t *delayp)
{
	assert(pcm);
	assert(pcm->setup);
	return pcm->fast_ops->delay(pcm->fast_op_arg, delayp);
}

int snd_pcm_prepare(snd_pcm_t *pcm)
{
	assert(pcm);
	assert(pcm->setup);
	return pcm->fast_ops->prepare(pcm->fast_op_arg);
}

int snd_pcm_reset(snd_pcm_t *pcm)
{
	assert(pcm);
	assert(pcm->setup);
	return pcm->fast_ops->reset(pcm->fast_op_arg);
}

int snd_pcm_start(snd_pcm_t *pcm)
{
	assert(pcm);
	assert(pcm->setup);
	return pcm->fast_ops->start(pcm->fast_op_arg);
}

int snd_pcm_drop(snd_pcm_t *pcm)
{
	assert(pcm);
	assert(pcm->setup);
	return pcm->fast_ops->drop(pcm->fast_op_arg);
}

int snd_pcm_drain(snd_pcm_t *pcm)
{
	assert(pcm);
	assert(pcm->setup);
	return pcm->fast_ops->drain(pcm->fast_op_arg);
}

int snd_pcm_pause(snd_pcm_t *pcm, int enable)
{
	assert(pcm);
	assert(pcm->setup);
	return pcm->fast_ops->pause(pcm->fast_op_arg, enable);
}


ssize_t snd_pcm_rewind(snd_pcm_t *pcm, size_t frames)
{
	assert(pcm);
	assert(pcm->setup);
	assert(frames > 0);
	return pcm->fast_ops->rewind(pcm->fast_op_arg, frames);
}

int snd_pcm_set_avail_min(snd_pcm_t *pcm, size_t frames)
{
	int err;
	assert(pcm);
	assert(pcm->setup);
	assert(frames > 0);
	err = pcm->fast_ops->set_avail_min(pcm->fast_op_arg, frames);
	if (err < 0)
		return err;
	pcm->avail_min = frames;
	return 0;
}

ssize_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, size_t size)
{
	assert(pcm);
	assert(size == 0 || buffer);
	assert(pcm->setup);
	assert(pcm->access == SND_PCM_ACCESS_RW_INTERLEAVED);
	return _snd_pcm_writei(pcm, buffer, size);
}

ssize_t snd_pcm_writen(snd_pcm_t *pcm, void **bufs, size_t size)
{
	assert(pcm);
	assert(size == 0 || bufs);
	assert(pcm->setup);
	assert(pcm->access == SND_PCM_ACCESS_RW_NONINTERLEAVED);
	return _snd_pcm_writen(pcm, bufs, size);
}

ssize_t snd_pcm_readi(snd_pcm_t *pcm, void *buffer, size_t size)
{
	assert(pcm);
	assert(size == 0 || buffer);
	assert(pcm->setup);
	assert(pcm->access == SND_PCM_ACCESS_RW_INTERLEAVED);
	return _snd_pcm_readi(pcm, buffer, size);
}

ssize_t snd_pcm_readn(snd_pcm_t *pcm, void **bufs, size_t size)
{
	assert(pcm);
	assert(size == 0 || bufs);
	assert(pcm->setup);
	assert(pcm->access == SND_PCM_ACCESS_RW_NONINTERLEAVED);
	return _snd_pcm_readn(pcm, bufs, size);
}

ssize_t snd_pcm_writev(snd_pcm_t *pcm, const struct iovec *vector, int count)
{
	void **bufs;
	int k;
	assert(pcm);
	assert(pcm->setup);
	assert((int)pcm->channels == count);
	bufs = alloca(sizeof(*bufs) * count);
	for (k = 0; k < count; ++k) {
		bufs[k] = vector[k].iov_base;
		assert(vector[k].iov_len == vector[0].iov_len);
	}
	return snd_pcm_writen(pcm, bufs, vector[0].iov_len);
}

ssize_t snd_pcm_readv(snd_pcm_t *pcm, const struct iovec *vector, int count)
{
	void **bufs;
	int k;
	assert(pcm);
	assert(pcm->setup);
	assert((int)pcm->channels == count);
	bufs = alloca(sizeof(*bufs) * count);
	for (k = 0; k < count; ++k) {
		bufs[k] = vector[k].iov_base;
		assert(vector[k].iov_len == vector[0].iov_len);
	}
	return snd_pcm_readn(pcm, bufs, vector[0].iov_len);
}

int snd_pcm_card(snd_pcm_t *pcm)
{
	assert(pcm);
	return pcm->ops->card(pcm->op_arg);
}

/* FIXME */
#define snd_pcm_link_descriptor snd_pcm_poll_descriptor

int snd_pcm_link(snd_pcm_t *pcm1, snd_pcm_t *pcm2)
{
	int fd1 = snd_pcm_link_descriptor(pcm1);
	int fd2 = snd_pcm_link_descriptor(pcm2);
	if (fd1 < 0 || fd2 < 0)
		return -ENOSYS;
	if (ioctl(fd1, SND_PCM_IOCTL_LINK, fd2) < 0) {
		SYSERR("SND_PCM_IOCTL_LINK failed");
		return -errno;
	}
	return 0;
}

int snd_pcm_unlink(snd_pcm_t *pcm)
{
	int fd;
	switch (pcm->type) {
	case SND_PCM_TYPE_HW:
	case SND_PCM_TYPE_MULTI:
		fd = snd_pcm_poll_descriptor(pcm);
		break;
	default:
		return -ENOSYS;
	}
	if (ioctl(fd, SND_PCM_IOCTL_UNLINK) < 0) {
		SYSERR("SND_PCM_IOCTL_UNLINK failed");
		return -errno;
	}
	return 0;
}

int snd_pcm_poll_descriptor(snd_pcm_t *pcm)
{
	assert(pcm);
	return pcm->poll_fd;
}

#define STATE(v) [SND_PCM_STATE_##v] = #v
#define STREAM(v) [SND_PCM_STREAM_##v] = #v
#define READY(v) [SND_PCM_READY_##v] = #v
#define XRUN(v) [SND_PCM_XRUN_##v] = #v
#define ACCESS(v) [SND_PCM_ACCESS_##v] = #v
#define START(v) [SND_PCM_START_##v] = #v
#define HW_PARAM(v) [SND_PCM_HW_PARAM_##v] = #v
#define SW_PARAM(v) [SND_PCM_SW_PARAM_##v] = #v
#define FORMAT(v) [SND_PCM_FORMAT_##v] = #v
#define SUBFORMAT(v) [SND_PCM_SUBFORMAT_##v] = #v 

#define FORMATD(v, d) [SND_PCM_FORMAT_##v] = d
#define SUBFORMATD(v, d) [SND_PCM_SUBFORMAT_##v] = d 

char *snd_pcm_stream_names[] = {
	STREAM(PLAYBACK),
	STREAM(CAPTURE),
};

char *snd_pcm_state_names[] = {
	STATE(OPEN),
	STATE(SETUP),
	STATE(PREPARED),
	STATE(RUNNING),
	STATE(XRUN),
	STATE(PAUSED),
};

char *snd_pcm_hw_param_names[] = {
	HW_PARAM(ACCESS),
	HW_PARAM(FORMAT),
	HW_PARAM(SUBFORMAT),
	HW_PARAM(CHANNELS),
	HW_PARAM(RATE),
	HW_PARAM(FRAGMENT_LENGTH),
	HW_PARAM(FRAGMENT_SIZE),
	HW_PARAM(FRAGMENTS),
	HW_PARAM(BUFFER_LENGTH),
	HW_PARAM(BUFFER_SIZE),
	HW_PARAM(SAMPLE_BITS),
	HW_PARAM(FRAME_BITS),
	HW_PARAM(FRAGMENT_BYTES),
	HW_PARAM(BUFFER_BYTES),
};

char *snd_pcm_sw_param_names[] = {
	SW_PARAM(START_MODE),
	SW_PARAM(READY_MODE),
	SW_PARAM(AVAIL_MIN),
	SW_PARAM(XFER_MIN),
	SW_PARAM(XFER_ALIGN),
	SW_PARAM(XRUN_MODE),
	SW_PARAM(TIME),
};

char *snd_pcm_access_names[] = {
	ACCESS(MMAP_INTERLEAVED), 
	ACCESS(MMAP_NONINTERLEAVED),
	ACCESS(MMAP_COMPLEX),
	ACCESS(RW_INTERLEAVED),
	ACCESS(RW_NONINTERLEAVED),
};

char *snd_pcm_format_names[] = {
	FORMAT(S8),
	FORMAT(U8),
	FORMAT(S16_LE),
	FORMAT(S16_BE),
	FORMAT(U16_LE),
	FORMAT(U16_BE),
	FORMAT(S24_LE),
	FORMAT(S24_BE),
	FORMAT(U24_LE),
	FORMAT(U24_BE),
	FORMAT(S32_LE),
	FORMAT(S32_BE),
	FORMAT(U32_LE),
	FORMAT(U32_BE),
	FORMAT(FLOAT_LE),
	FORMAT(FLOAT_BE),
	FORMAT(FLOAT64_LE),
	FORMAT(FLOAT64_BE),
	FORMAT(IEC958_SUBFRAME_LE),
	FORMAT(IEC958_SUBFRAME_BE),
	FORMAT(MU_LAW),
	FORMAT(A_LAW),
	FORMAT(IMA_ADPCM),
	FORMAT(MPEG),
	FORMAT(GSM),
	FORMAT(SPECIAL),
};

char *snd_pcm_format_descriptions[] = {
	FORMATD(S8, "Signed 8-bit"), 
	FORMATD(U8, "Unsigned 8-bit"),
	FORMATD(S16_LE, "Signed 16-bit Little Endian"),
	FORMATD(S16_BE, "Signed 16-bit Big Endian"),
	FORMATD(U16_LE, "Unsigned 16-bit Little Endian"),
	FORMATD(U16_BE, "Unsigned 16-bit Big Endian"),
	FORMATD(S24_LE, "Signed 24-bit Little Endian"),
	FORMATD(S24_BE, "Signed 24-bit Big Endian"),
	FORMATD(U24_LE, "Unsigned 24-bit Little Endian"),
	FORMATD(U24_BE, "Unsigned 24-bit Big Endian"),
	FORMATD(S32_LE, "Signed 32-bit Little Endian"),
	FORMATD(S32_BE, "Signed 32-bit Big Endian"),
	FORMATD(U32_LE, "Unsigned 32-bit Little Endian"),
	FORMATD(U32_BE, "Unsigned 32-bit Big Endian"),
	FORMATD(FLOAT_LE, "Float Little Endian"),
	FORMATD(FLOAT_BE, "Float Big Endian"),
	FORMATD(FLOAT64_LE, "Float64 Little Endian"),
	FORMATD(FLOAT64_BE, "Float64 Big Endian"),
	FORMATD(IEC958_SUBFRAME_LE, "IEC-958 Little Endian"),
	FORMATD(IEC958_SUBFRAME_BE, "IEC-958 Big Endian"),
	FORMATD(MU_LAW, "Mu-Law"),
	FORMATD(A_LAW, "A-Law"),
	FORMATD(IMA_ADPCM, "Ima-ADPCM"),
	FORMATD(MPEG, "MPEG"),
	FORMATD(GSM, "GSM"),
	FORMATD(SPECIAL, "Special"),
};

char *snd_pcm_subformat_names[] = {
	SUBFORMAT(STD), 
};

char *snd_pcm_subformat_descriptions[] = {
	SUBFORMATD(STD, "Standard"), 
};

char *snd_pcm_start_mode_names[] = {
	START(EXPLICIT),
	START(DATA),
};

char *snd_pcm_ready_mode_names[] = {
	READY(FRAGMENT),
	READY(ASAP),
};

char *snd_pcm_xrun_mode_names[] = {
	XRUN(ASAP),
	XRUN(FRAGMENT),
	XRUN(NONE),
};

static char *onoff[] = {
	[0] = "OFF",
	[1] = "ON",
};

#define assoc(value, names) ({ \
	unsigned int __v = value; \
	assert(__v < sizeof(names) / sizeof(names[0])); \
	names[__v]; \
})


int snd_pcm_dump_hw_setup(snd_pcm_t *pcm, FILE *fp)
{
	assert(pcm);
	assert(fp);
	assert(pcm->setup);
        fprintf(fp, "stream       : %s\n", assoc(pcm->stream, snd_pcm_stream_names));
	fprintf(fp, "access       : %s\n", assoc(pcm->access, snd_pcm_access_names));
	fprintf(fp, "format       : %s\n", assoc(pcm->format, snd_pcm_format_names));
	fprintf(fp, "subformat    : %s\n", assoc(pcm->subformat, snd_pcm_subformat_names));
	fprintf(fp, "channels     : %u\n", pcm->channels);
	fprintf(fp, "rate         : %u\n", pcm->rate);
	fprintf(fp, "exact rate   : %g (%u/%u)\n", (double) pcm->rate_num / pcm->rate_den, pcm->rate_num, pcm->rate_den);
	fprintf(fp, "msbits       : %u\n", pcm->msbits);
	fprintf(fp, "fragment_size: %lu\n", (long)pcm->fragment_size);
	fprintf(fp, "fragments    : %u\n", pcm->fragments);
	return 0;
}

int snd_pcm_dump_sw_setup(snd_pcm_t *pcm, FILE *fp)
{
	assert(pcm);
	assert(fp);
	assert(pcm->setup);
	fprintf(fp, "start_mode   : %s\n", assoc(pcm->start_mode, snd_pcm_start_mode_names));
	fprintf(fp, "ready_mode   : %s\n", assoc(pcm->ready_mode, snd_pcm_ready_mode_names));
	fprintf(fp, "xrun_mode    : %s\n", assoc(pcm->xrun_mode, snd_pcm_xrun_mode_names));
	fprintf(fp, "avail_min    : %ld\n", (long)pcm->avail_min);
	fprintf(fp, "xfer_min     : %ld\n", (long)pcm->xfer_min);
	fprintf(fp, "xfer_align   : %ld\n", (long)pcm->xfer_align);
	fprintf(fp, "time         : %s\n", assoc(pcm->time, onoff));
	fprintf(fp, "boundary     : %ld\n", (long)pcm->boundary);
	return 0;
}

int snd_pcm_dump_setup(snd_pcm_t *pcm, FILE *fp)
{
	snd_pcm_dump_hw_setup(pcm, fp);
	snd_pcm_dump_sw_setup(pcm, fp);
	return 0;
}

int snd_pcm_dump_sw_params_fail(snd_pcm_sw_params_t *params, FILE *fp)
{
	int k;
	if (params->fail_mask == 0) {
		fprintf(fp, "unknown sw_params failure reason\n");
		return 0;
	}
	fprintf(fp, "sw_params failed on the following field value(s):\n");
	for (k = 0; k <= SND_PCM_SW_PARAM_LAST; ++k) {
		if (!(params->fail_mask & (1U << k)))
			continue;
		switch (k) {
		case SND_PCM_SW_PARAM_START_MODE:
			fprintf(fp, "start_mode: %s\n", assoc(params->start_mode, snd_pcm_start_mode_names));
			break;
		case SND_PCM_SW_PARAM_READY_MODE:
			fprintf(fp, "ready_mode: %s\n", assoc(params->ready_mode, snd_pcm_ready_mode_names));
			break;
		case SND_PCM_SW_PARAM_XRUN_MODE:
			fprintf(fp, "xrun_mode: %s\n", assoc(params->xrun_mode, snd_pcm_xrun_mode_names));
			break;
		case SND_PCM_SW_PARAM_AVAIL_MIN:
			fprintf(fp, "avail_min: %ld\n", (long)params->avail_min);
			break;
		case SND_PCM_SW_PARAM_XFER_MIN:
			fprintf(fp, "xfer_min: %ld\n", (long)params->xfer_min);
			break;
		case SND_PCM_SW_PARAM_XFER_ALIGN:
			fprintf(fp, "xfer_align: %ld\n", (long)params->xfer_align);
			break;
		case SND_PCM_SW_PARAM_TIME:
			fprintf(fp, "time: %d\n", params->time);
			break;
		default:
			assert(0);
			break;
		}
	}
	return 0;
}

int snd_pcm_dump_status(snd_pcm_status_t *status, FILE *fp)
{
	assert(status);
	fprintf(fp, "state       : %s\n", assoc(status->state, snd_pcm_state_names));
	fprintf(fp, "trigger_time: %ld.%06ld\n",
		status->trigger_time.tv_sec, status->trigger_time.tv_usec);
	fprintf(fp, "tstamp      : %ld.%06ld\n",
		status->tstamp.tv_sec, status->tstamp.tv_usec);
	fprintf(fp, "delay       : %ld\n", (long)status->delay);
	fprintf(fp, "avail       : %ld\n", (long)status->avail);
	fprintf(fp, "avail_max   : %ld\n", (long)status->avail_max);
	return 0;
}

int snd_pcm_dump(snd_pcm_t *pcm, FILE *fp)
{
	assert(pcm);
	assert(fp);
	pcm->ops->dump(pcm->op_arg, fp);
	return 0;
}

const char *snd_pcm_access_name(unsigned int access)
{
	assert(access <= SND_PCM_ACCESS_LAST);
	return snd_pcm_access_names[access];
}

const char *snd_pcm_format_name(unsigned int format)
{
	assert(format <= SND_PCM_FORMAT_LAST);
	return snd_pcm_format_names[format];
}

const char *snd_pcm_format_description(unsigned int format)
{
	assert(format <= SND_PCM_FORMAT_LAST);
	return snd_pcm_format_descriptions[format];
}

int snd_pcm_format_value(const char* name)
{
	unsigned int format;
	for (format = 0; format <= SND_PCM_FORMAT_LAST; format++)
		if (snd_pcm_format_names[format] &&
		    strcasecmp(name, snd_pcm_format_names[format]) == 0)
			return format;
	return -1;
}

const char *snd_pcm_subformat_name(unsigned int subformat)
{
	assert(subformat <= SND_PCM_SUBFORMAT_LAST);
	return snd_pcm_subformat_names[subformat];
}

const char *snd_pcm_hw_param_name(unsigned int param)
{
	assert(param <= SND_PCM_HW_PARAM_LAST);
	return snd_pcm_hw_param_names[param];
}

ssize_t snd_pcm_bytes_to_frames(snd_pcm_t *pcm, ssize_t bytes)
{
	assert(pcm);
	assert(pcm->setup);
	return bytes * 8 / pcm->bits_per_frame;
}

ssize_t snd_pcm_frames_to_bytes(snd_pcm_t *pcm, ssize_t frames)
{
	assert(pcm);
	assert(pcm->setup);
	return frames * pcm->bits_per_frame / 8;
}

ssize_t snd_pcm_bytes_to_samples(snd_pcm_t *pcm, ssize_t bytes)
{
	assert(pcm);
	assert(pcm->setup);
	return bytes * 8 / pcm->bits_per_sample;
}

ssize_t snd_pcm_samples_to_bytes(snd_pcm_t *pcm, ssize_t samples)
{
	assert(pcm);
	assert(pcm->setup);
	return samples * pcm->bits_per_sample / 8;
}

int snd_pcm_open(snd_pcm_t **pcmp, char *name, 
		 int stream, int mode)
{
	char *str;
	int err;
	snd_config_t *pcm_conf, *conf, *type_conf;
	snd_config_iterator_t i;
	char *lib = NULL, *open = NULL;
	int (*open_func)(snd_pcm_t **pcmp, char *name, snd_config_t *conf, 
			 int stream, int mode);
	void *h;
	assert(pcmp && name);
	err = snd_config_update();
	if (err < 0)
		return err;
	err = snd_config_searchv(snd_config, &pcm_conf, "pcm", name, 0);
	if (err < 0) {
		int card, dev, subdev;
		char socket[256], sname[256];
		char format[16], file[256];
		err = sscanf(name, "hw:%d,%d,%d", &card, &dev, &subdev);
		if (err == 3)
			return snd_pcm_hw_open(pcmp, name, card, dev, subdev, stream, mode);
		err = sscanf(name, "hw:%d,%d", &card, &dev);
		if (err == 2)
			return snd_pcm_hw_open(pcmp, name, card, dev, -1, stream, mode);
		err = sscanf(name, "plug:%d,%d,%d", &card, &dev, &subdev);
		if (err == 3)
			return snd_pcm_plug_open_hw(pcmp, name, card, dev, subdev, stream, mode);
		err = sscanf(name, "plug:%d,%d", &card, &dev);
		if (err == 2)
			return snd_pcm_plug_open_hw(pcmp, name, card, dev, -1, stream, mode);
		err = sscanf(name, "shm:%256s,%256s", socket, sname);
		if (err == 2)
			return snd_pcm_shm_open(pcmp, NULL, socket, sname, stream, mode);
		err = sscanf(name, "file:%256s,%16s", file, format);
		if (err == 2) {
			snd_pcm_t *slave;
			err = snd_pcm_null_open(&slave, NULL, stream, mode);
			if (err < 0)
				return err;
			return snd_pcm_file_open(pcmp, NULL, file, -1, format, slave, 1);
		}
		err = sscanf(name, "file:%256s", file);
		if (err == 1) {
			snd_pcm_t *slave;
			err = snd_pcm_null_open(&slave, NULL, stream, mode);
			if (err < 0)
				return err;
			return snd_pcm_file_open(pcmp, NULL, file, -1, "raw", slave, 1);
		}
		if (strcmp(name, "null") == 0)
			return snd_pcm_null_open(pcmp, NULL, stream, mode);
		ERR("Unknown PCM %s", name);
		return -ENOENT;
	}
	if (snd_config_type(pcm_conf) != SND_CONFIG_TYPE_COMPOUND) {
		ERR("Invalid type for PCM %s definition", name);
		return -EINVAL;
	}
	err = snd_config_search(pcm_conf, "stream", &conf);
	if (err >= 0) {
		err = snd_config_string_get(conf, &str);
		if (err < 0) {
			ERR("Invalid type for %s", conf->id);
			return err;
		}
		if (strcmp(str, "playback") == 0) {
			if (stream != SND_PCM_STREAM_PLAYBACK)
				return -EINVAL;
		} else if (strcmp(str, "capture") == 0) {
			if (stream != SND_PCM_STREAM_CAPTURE)
				return -EINVAL;
		} else {
			ERR("Invalid value for %s", conf->id);
			return -EINVAL;
		}
	}
	err = snd_config_search(pcm_conf, "type", &conf);
	if (err < 0) {
		ERR("type is not defined");
		return err;
	}
	err = snd_config_string_get(conf, &str);
	if (err < 0) {
		ERR("Invalid type for %s", conf->id);
		return err;
	}
	err = snd_config_searchv(snd_config, &type_conf, "pcmtype", str, 0);
	if (err < 0) {
		ERR("Unknown PCM type %s", str);
		return err;
	}
	snd_config_foreach(i, type_conf) {
		snd_config_t *n = snd_config_entry(i);
		if (strcmp(n->id, "comment") == 0)
			continue;
		if (strcmp(n->id, "lib") == 0) {
			err = snd_config_string_get(n, &lib);
			if (err < 0) {
				ERR("Invalid type for %s", n->id);
				return -EINVAL;
			}
			continue;
		}
		if (strcmp(n->id, "open") == 0) {
			err = snd_config_string_get(n, &open);
			if (err < 0) {
				ERR("Invalid type for %s", n->id);
				return -EINVAL;
			}
			continue;
			ERR("Unknown field %s", n->id);
			return -EINVAL;
		}
	}
	if (!open) {
		ERR("open is not defined");
		return -EINVAL;
	}
	if (!lib)
		lib = "libasound.so";
	h = dlopen(lib, RTLD_NOW);
	if (!h) {
		ERR("Cannot open shared library %s", lib);
		return -ENOENT;
	}
	open_func = dlsym(h, open);
	dlclose(h);
	if (!open_func) {
		ERR("symbol %s is not defined inside %s", open, lib);
		return -ENXIO;
	}
	return open_func(pcmp, name, pcm_conf, stream, mode);
}

void snd_pcm_areas_from_buf(snd_pcm_t *pcm, snd_pcm_channel_area_t *areas, 
			    void *buf)
{
	unsigned int channel;
	unsigned int channels = pcm->channels;
	for (channel = 0; channel < channels; ++channel, ++areas) {
		areas->addr = buf;
		areas->first = channel * pcm->bits_per_sample;
		areas->step = pcm->bits_per_frame;
	}
}

void snd_pcm_areas_from_bufs(snd_pcm_t *pcm, snd_pcm_channel_area_t *areas, 
			     void **bufs)
{
	unsigned int channel;
	unsigned int channels = pcm->channels;
	for (channel = 0; channel < channels; ++channel, ++areas, ++bufs) {
		areas->addr = *bufs;
		areas->first = 0;
		areas->step = pcm->bits_per_sample;
	}
}

int snd_pcm_wait(snd_pcm_t *pcm, int timeout)
{
	struct pollfd pfd;
	int err;
	pfd.fd = snd_pcm_poll_descriptor(pcm);
	pfd.events = pcm->stream == SND_PCM_STREAM_PLAYBACK ? POLLOUT : POLLIN;
	err = poll(&pfd, 1, timeout);
	if (err < 0)
		return err;
	return 0;
}

ssize_t snd_pcm_avail_update(snd_pcm_t *pcm)
{
	return pcm->fast_ops->avail_update(pcm->fast_op_arg);
}

ssize_t snd_pcm_mmap_forward(snd_pcm_t *pcm, size_t size)
{
	assert(size > 0);
	assert(size <= snd_pcm_mmap_avail(pcm));
	return pcm->fast_ops->mmap_forward(pcm->fast_op_arg, size);
}

int snd_pcm_area_silence(const snd_pcm_channel_area_t *dst_area, size_t dst_offset,
			 size_t samples, int format)
{
	/* FIXME: sub byte resolution and odd dst_offset */
	char *dst;
	unsigned int dst_step;
	int width;
	u_int64_t silence;
	if (!dst_area->addr)
		return 0;
	dst = snd_pcm_channel_area_addr(dst_area, dst_offset);
	width = snd_pcm_format_physical_width(format);
	silence = snd_pcm_format_silence_64(format);
	if (dst_area->step == (unsigned int) width) {
		size_t dwords = samples * width / 64;
		samples -= dwords * 64 / width;
		while (dwords-- > 0)
			*((u_int64_t*)dst)++ = silence;
		if (samples == 0)
			return 0;
	}
	dst_step = dst_area->step / 8;
	switch (width) {
	case 4: {
		u_int8_t s0 = silence & 0xf0;
		u_int8_t s1 = silence & 0x0f;
		int dstbit = dst_area->first % 8;
		int dstbit_step = dst_area->step % 8;
		while (samples-- > 0) {
			if (dstbit) {
				*dst &= 0xf0;
				*dst |= s1;
			} else {
				*dst &= 0x0f;
				*dst |= s0;
			}
			dst += dst_step;
			dstbit += dstbit_step;
			if (dstbit == 8) {
				dst++;
				dstbit = 0;
			}
		}
		break;
	}
	case 8: {
		u_int8_t sil = silence;
		while (samples-- > 0) {
			*dst = sil;
			dst += dst_step;
		}
		break;
	}
	case 16: {
		u_int16_t sil = silence;
		while (samples-- > 0) {
			*(u_int16_t*)dst = sil;
			dst += dst_step;
		}
		break;
	}
	case 32: {
		u_int32_t sil = silence;
		while (samples-- > 0) {
			*(u_int32_t*)dst = sil;
			dst += dst_step;
		}
		break;
	}
	case 64: {
		while (samples-- > 0) {
			*(u_int64_t*)dst = silence;
			dst += dst_step;
		}
		break;
	}
	default:
		assert(0);
	}
	return 0;
}

int snd_pcm_areas_silence(const snd_pcm_channel_area_t *dst_areas, size_t dst_offset,
			  size_t channels, size_t frames, int format)
{
	int width = snd_pcm_format_physical_width(format);
	while (channels > 0) {
		void *addr = dst_areas->addr;
		unsigned int step = dst_areas->step;
		const snd_pcm_channel_area_t *begin = dst_areas;
		int channels1 = channels;
		unsigned int chns = 0;
		int err;
		while (1) {
			channels1--;
			chns++;
			dst_areas++;
			if (channels1 == 0 ||
			    dst_areas->addr != addr ||
			    dst_areas->step != step ||
			    dst_areas->first != dst_areas[-1].first + width)
				break;
		}
		if (chns > 1 && chns * width == step) {
			/* Collapse the areas */
			snd_pcm_channel_area_t d;
			d.addr = begin->addr;
			d.first = begin->first;
			d.step = width;
			err = snd_pcm_area_silence(&d, dst_offset * chns, frames * chns, format);
			channels -= chns;
		} else {
			err = snd_pcm_area_silence(begin, dst_offset, frames, format);
			dst_areas = begin + 1;
			channels--;
		}
		if (err < 0)
			return err;
	}
	return 0;
}


int snd_pcm_area_copy(const snd_pcm_channel_area_t *src_area, size_t src_offset,
		      const snd_pcm_channel_area_t *dst_area, size_t dst_offset,
		      size_t samples, int format)
{
	/* FIXME: sub byte resolution and odd dst_offset */
	char *src, *dst;
	int width;
	int src_step, dst_step;
	if (!src_area->addr)
		return snd_pcm_area_silence(dst_area, dst_offset, samples, format);
	src = snd_pcm_channel_area_addr(src_area, src_offset);
	if (!dst_area->addr)
		return 0;
	dst = snd_pcm_channel_area_addr(dst_area, dst_offset);
	width = snd_pcm_format_physical_width(format);
	if (src_area->step == (unsigned int) width &&
	    dst_area->step == (unsigned int) width) {
		size_t bytes = samples * width / 8;
		samples -= bytes * 8 / width;
		memcpy(dst, src, bytes);
		if (samples == 0)
			return 0;
	}
	src_step = src_area->step / 8;
	dst_step = dst_area->step / 8;
	switch (width) {
	case 4: {
		int srcbit = src_area->first % 8;
		int srcbit_step = src_area->step % 8;
		int dstbit = dst_area->first % 8;
		int dstbit_step = dst_area->step % 8;
		while (samples-- > 0) {
			unsigned char srcval;
			if (srcbit)
				srcval = *src & 0x0f;
			else
				srcval = *src & 0xf0;
			if (dstbit)
				*dst &= 0xf0;
			else
				*dst &= 0x0f;
			*dst |= srcval;
			src += src_step;
			srcbit += srcbit_step;
			if (srcbit == 8) {
				src++;
				srcbit = 0;
			}
			dst += dst_step;
			dstbit += dstbit_step;
			if (dstbit == 8) {
				dst++;
				dstbit = 0;
			}
		}
		break;
	}
	case 8: {
		while (samples-- > 0) {
			*dst = *src;
			src += src_step;
			dst += dst_step;
		}
		break;
	}
	case 16: {
		while (samples-- > 0) {
			*(u_int16_t*)dst = *(u_int16_t*)src;
			src += src_step;
			dst += dst_step;
		}
		break;
	}
	case 32: {
		while (samples-- > 0) {
			*(u_int32_t*)dst = *(u_int32_t*)src;
			src += src_step;
			dst += dst_step;
		}
		break;
	}
	case 64: {
		while (samples-- > 0) {
			*(u_int64_t*)dst = *(u_int64_t*)src;
			src += src_step;
			dst += dst_step;
		}
		break;
	}
	default:
		assert(0);
	}
	return 0;
}

int snd_pcm_areas_copy(const snd_pcm_channel_area_t *src_areas, size_t src_offset,
		       const snd_pcm_channel_area_t *dst_areas, size_t dst_offset,
		       size_t channels, size_t frames, int format)
{
	int width = snd_pcm_format_physical_width(format);
	while (channels > 0) {
		unsigned int step = src_areas->step;
		void *src_addr = src_areas->addr;
		const snd_pcm_channel_area_t *src_start = src_areas;
		void *dst_addr = dst_areas->addr;
		const snd_pcm_channel_area_t *dst_start = dst_areas;
		int channels1 = channels;
		unsigned int chns = 0;
		while (dst_areas->step == step) {
			channels1--;
			chns++;
			src_areas++;
			dst_areas++;
			if (channels1 == 0 ||
			    src_areas->step != step ||
			    src_areas->addr != src_addr ||
			    dst_areas->addr != dst_addr ||
			    src_areas->first != src_areas[-1].first + width ||
			    dst_areas->first != dst_areas[-1].first + width)
				break;
		}
		if (chns > 1 && chns * width == step) {
			/* Collapse the areas */
			snd_pcm_channel_area_t s, d;
			s.addr = src_start->addr;
			s.first = src_start->first;
			s.step = width;
			d.addr = dst_start->addr;
			d.first = dst_start->first;
			d.step = width;
			snd_pcm_area_copy(&s, src_offset * chns, &d, dst_offset * chns, frames * chns, format);
			channels -= chns;
		} else {
			snd_pcm_area_copy(src_start, src_offset, dst_start, dst_offset, frames, format);
			src_areas = src_start + 1;
			dst_areas = dst_start + 1;
			channels--;
		}
	}
	return 0;
}

ssize_t snd_pcm_read_areas(snd_pcm_t *pcm, const snd_pcm_channel_area_t *areas,
			   size_t offset, size_t size,
			   snd_pcm_xfer_areas_func_t func)
{
	size_t xfer = 0;
	ssize_t err = 0;
	int state = snd_pcm_state(pcm);
	assert(size > 0);
	assert(state >= SND_PCM_STATE_PREPARED);
	if (state == SND_PCM_STATE_PREPARED &&
	    pcm->start_mode != SND_PCM_START_EXPLICIT) {
		err = snd_pcm_start(pcm);
		if (err < 0)
			return err;
		state = SND_PCM_STATE_RUNNING;
	}
	while (xfer < size) {
		ssize_t avail;
		size_t frames;
	again:
		avail = snd_pcm_avail_update(pcm);
		if (avail < 0) {
			err = avail;
			break;
		}
		if ((size_t)avail < pcm->avail_min) {
			if (state != SND_PCM_STATE_RUNNING) {
				err = -EPIPE;
				break;
			}
			if (pcm->mode & SND_PCM_NONBLOCK) {
				err = -EAGAIN;
				break;
			}
			err = snd_pcm_wait(pcm, -1);
			if (err < 0)
				break;
			state = snd_pcm_state(pcm);
			goto again;
		}
		frames = size - xfer;
		if (frames > (size_t)avail)
			frames = avail;
		err = func(pcm, areas, offset, frames, 0);
		if (err < 0)
			break;
		assert((size_t)err == frames);
		xfer += err;
		offset += err;
	}
	if (xfer > 0)
		return xfer;
	return err;
}

ssize_t snd_pcm_write_areas(snd_pcm_t *pcm, const snd_pcm_channel_area_t *areas,
			    size_t offset, size_t size,
			    snd_pcm_xfer_areas_func_t func)
{
	size_t xfer = 0;
	ssize_t err = 0;
	int state = snd_pcm_state(pcm);
	assert(size > 0);
	assert(state >= SND_PCM_STATE_PREPARED);
	while (xfer < size) {
		ssize_t avail;
		size_t frames;
	again:
		if (state == SND_PCM_STATE_XRUN) {
			err = -EPIPE;
			break;
		}
		avail = snd_pcm_avail_update(pcm);
		if (avail < 0) {
			err = avail;
			break;
		}
		if ((size_t)avail < pcm->avail_min) {
			if (state != SND_PCM_STATE_RUNNING) {
				err = -EPIPE;
				break;
			}
			if (pcm->mode & SND_PCM_NONBLOCK) {
				err = -EAGAIN;
				break;
			}
			err = snd_pcm_wait(pcm, -1);
			if (err < 0)
				break;
			state = snd_pcm_state(pcm);
			goto again;
		}
		frames = size - xfer;
		if (frames > (size_t)avail)
			frames = avail;
		err = func(pcm, areas, offset, frames, 0);
		if (err < 0)
			break;
		assert((size_t)err == frames);
		xfer += err;
		offset += err;
		if (state == SND_PCM_STATE_PREPARED &&
		    pcm->start_mode != SND_PCM_START_EXPLICIT) {
			err = snd_pcm_start(pcm);
			if (err < 0)
				break;
			state = SND_PCM_STATE_RUNNING;
		}
	}
	if (xfer > 0)
		return xfer;
	return err;
}

size_t _snd_pcm_mmap_hw_ptr(snd_pcm_t *pcm)
{
	return *pcm->hw_ptr;
}

