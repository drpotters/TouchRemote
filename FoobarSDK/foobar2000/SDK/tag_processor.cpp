#include "foobar2000-sdk-pch.h"
#include "file_info_impl.h"
#include "tag_processor.h"

void tag_processor_trailing::write_id3v1(const service_ptr_t<file> & p_file,const file_info & p_info,abort_callback & p_abort)
{
	write(p_file,p_info,flag_id3v1,p_abort);
}

void tag_processor_trailing::write_apev2(const service_ptr_t<file> & p_file,const file_info & p_info,abort_callback & p_abort)
{
	write(p_file,p_info,flag_apev2,p_abort);
}

void tag_processor_trailing::write_apev2_id3v1(const service_ptr_t<file> & p_file,const file_info & p_info,abort_callback & p_abort)
{
	write(p_file,p_info,flag_id3v1|flag_apev2,p_abort);
}


t_filesize tag_processor_trailing::read_v2_(const file::ptr & file, file_info& outInfo, abort_callback& abort) {
	{
		tag_processor_trailing_v2::ptr v2;
		if (v2 &= this) return v2->read_v2(file, outInfo, abort);
	}
	// no new API, emulate with old
	try {
		t_filesize ret = filesize_invalid;
		this->read_ex(file, outInfo, ret, abort);
		PFC_ASSERT(ret != filesize_invalid);
		return ret;
	} catch (exception_io_data const&) {
		return filesize_invalid;
	}
}


enum {
	g_flag_id3v1 = 1<<0,
	g_flag_id3v2 = 1<<1,
	g_flag_apev2 = 1<<2
};


static void g_write_tags_ex(tag_write_callback & p_callback,unsigned p_flags,const service_ptr_t<file> & p_file,const file_info * p_info,abort_callback & p_abort) {
	PFC_ASSERT( p_flags == 0 || p_info != 0 );


	if (p_flags & (g_flag_id3v1 | g_flag_apev2)) {
		switch(p_flags & (g_flag_id3v1 | g_flag_apev2)) {
		case g_flag_id3v1 | g_flag_apev2:
			tag_processor_trailing::get()->write_apev2_id3v1(p_file,*p_info,p_abort);
			break;
		case g_flag_id3v1:
			tag_processor_trailing::get()->write_id3v1(p_file,*p_info,p_abort);
			break;
		case g_flag_apev2:
			tag_processor_trailing::get()->write_apev2(p_file,*p_info,p_abort);
			break;
		default:
			throw exception_io_data();
		}
	} else {
		tag_processor_trailing::get()->remove(p_file,p_abort);
	}

	if (p_flags & g_flag_id3v2)
	{
		tag_processor_id3v2::get()->write_ex(p_callback,p_file,*p_info,p_abort);
	}
	else
	{
		t_uint64 dummy;
		tag_processor_id3v2::g_remove_ex(p_callback,p_file,dummy,p_abort);
	}
}

static void g_write_tags(unsigned p_flags,const service_ptr_t<file> & p_file,const file_info * p_info,abort_callback & p_abort) {
    tag_write_callback_dummy cb;
	g_write_tags_ex(cb,p_flags,p_file,p_info,p_abort);
}


void tag_processor::write_multi(const service_ptr_t<file> & p_file,const file_info & p_info,abort_callback & p_abort,bool p_write_id3v1,bool p_write_id3v2,bool p_write_apev2) {
	unsigned flags = 0;
	if (p_write_id3v1) flags |= g_flag_id3v1;
	if (p_write_id3v2) flags |= g_flag_id3v2;
	if (p_write_apev2) flags |= g_flag_apev2;
	g_write_tags(flags,p_file,&p_info,p_abort);
}

void tag_processor::write_multi_ex(tag_write_callback & p_callback,const service_ptr_t<file> & p_file,const file_info & p_info,abort_callback & p_abort,bool p_write_id3v1,bool p_write_id3v2,bool p_write_apev2) {
	unsigned flags = 0;
	if (p_write_id3v1) flags |= g_flag_id3v1;
	if (p_write_id3v2) flags |= g_flag_id3v2;
	if (p_write_apev2) flags |= g_flag_apev2;
	g_write_tags_ex(p_callback,flags,p_file,&p_info,p_abort);
}

void tag_processor::write_id3v1(const service_ptr_t<file> & p_file,const file_info & p_info,abort_callback & p_abort) {
	g_write_tags(g_flag_id3v1,p_file,&p_info,p_abort);
}

void tag_processor::write_apev2(const service_ptr_t<file> & p_file,const file_info & p_info,abort_callback & p_abort) {
	g_write_tags(g_flag_apev2,p_file,&p_info,p_abort);
}

void tag_processor::write_apev2_id3v1(const service_ptr_t<file> & p_file,const file_info & p_info,abort_callback & p_abort) {
	g_write_tags(g_flag_apev2|g_flag_id3v1,p_file,&p_info,p_abort);
}

void tag_processor::write_id3v2(const service_ptr_t<file> & p_file,const file_info & p_info,abort_callback & p_abort) {
	g_write_tags(g_flag_id3v2,p_file,&p_info,p_abort);
}

void tag_processor::write_id3v2_id3v1(const service_ptr_t<file> & p_file,const file_info & p_info,abort_callback & p_abort) {
	g_write_tags(g_flag_id3v2|g_flag_id3v1,p_file,&p_info,p_abort);
}

void tag_processor::remove_trailing(const service_ptr_t<file> & p_file,abort_callback & p_abort) {
	return tag_processor_trailing::get()->remove(p_file,p_abort);
}

bool tag_processor::remove_id3v2(const service_ptr_t<file> & p_file,abort_callback & p_abort) {
	t_uint64 dummy = 0;
	tag_processor_id3v2::g_remove(p_file,dummy,p_abort);
	return dummy > 0;
}

void tag_processor::remove_id3v2_trailing(const service_ptr_t<file> & p_file,abort_callback & p_abort) {
	remove_id3v2(p_file,p_abort);
	remove_trailing(p_file,p_abort);
}

void tag_processor::read_trailing(const service_ptr_t<file> & p_file,file_info & p_info,abort_callback & p_abort) {
	tag_processor_trailing::get()->read(p_file,p_info,p_abort);
}

void tag_processor::read_trailing_ex(const service_ptr_t<file> & p_file,file_info & p_info,t_filesize & p_tagoffset,abort_callback & p_abort) {
	tag_processor_trailing::get()->read_ex(p_file,p_info,p_tagoffset,p_abort);
}

t_filesize tag_processor::read_trailing_nothrow(const service_ptr_t<file>& p_file, file_info& p_info, abort_callback& p_abort) {
	return tag_processor_trailing::get()->read_v2_(p_file, p_info, p_abort);
}

void tag_processor::read_id3v2(const service_ptr_t<file> & p_file,file_info & p_info,abort_callback & p_abort) {
	tag_processor_id3v2::get()->read(p_file,p_info,p_abort);
}

void tag_processor::read_id3v2_trailing(const service_ptr_t<file>& p_file, file_info& p_info, abort_callback& p_abort) {
	if (!read_id3v2_trailing_nothrow(p_file, p_info, p_abort)) throw exception_tag_not_found();
}

bool tag_processor::read_id3v2_trailing_nothrow(const service_ptr_t<file> & p_file,file_info & p_info,abort_callback & p_abort)
{
	file_info_impl id3v2, trailing;

	const bool have_id3v2 = tag_processor_id3v2::get()->read_v2_(p_file, id3v2, p_abort);
    const bool have_id3v2_text = have_id3v2 && id3v2.meta_get_count() > 0;
    
	bool have_trailing = false;
	if (!have_id3v2_text || !p_file->is_remote()) {
		have_trailing = tag_processor_trailing::get()->read_v2_(p_file, trailing, p_abort) != filesize_invalid;
	}

	if (!have_id3v2 && !have_trailing) return false;

	if (have_id3v2) {
		p_info._set_tag(id3v2);
		if (have_trailing) p_info._add_tag(trailing);
        if (! have_id3v2_text ) p_info.copy_meta(trailing);
	} else {
		p_info._set_tag(trailing);
	}

	return true;
}

void tag_processor::skip_id3v2(const service_ptr_t<file> & p_file,t_filesize & p_size_skipped,abort_callback & p_abort) {
	tag_processor_id3v2::g_skip(p_file,p_size_skipped,p_abort);
}

t_filesize tag_processor::skip_id3v2(file::ptr const & f, abort_callback & a) {
    t_filesize ret = 0;
    skip_id3v2(f, ret, a);
    return ret;
}

bool tag_processor::is_id3v1_sufficient(const file_info & p_info)
{
	return tag_processor_trailing::get()->is_id3v1_sufficient(p_info);
}

void tag_processor::truncate_to_id3v1(file_info & p_info)
{
	tag_processor_trailing::get()->truncate_to_id3v1(p_info);
}
