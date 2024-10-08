#include "../../event_class/event_class.h"

#ifdef __NR_lseek
TEST(SyscallEnter, lseekE) {
	auto evt_test = get_syscall_event_test(__NR_lseek, ENTER_EVENT);

	evt_test->enable_capture();

	/*=============================== TRIGGER SYSCALL  ===========================*/

	int fd = -1;
	off_t offset = 7;
	int whence = SEEK_SET;
	assert_syscall_state(SYSCALL_FAILURE, "lseek", syscall(__NR_lseek, fd, offset, whence));

	/*=============================== TRIGGER SYSCALL ===========================*/

	evt_test->disable_capture();

	evt_test->assert_event_presence();

	if(HasFatalFailure()) {
		return;
	}

	evt_test->parse_event();

	evt_test->assert_header();

	/*=============================== ASSERT PARAMETERS  ===========================*/

	/* Parameter 1: fd (type: PT_FD) */
	evt_test->assert_numeric_param(1, (int64_t)fd);

	/* Parameter 2: offset (type: PT_UINT64) */
	evt_test->assert_numeric_param(2, offset);

	/* Parameter 3: whence (type: PT_ENUMFLAGS8) */
	evt_test->assert_numeric_param(3, (uint8_t)PPM_SEEK_SET);

	/*=============================== ASSERT PARAMETERS  ===========================*/

	evt_test->assert_num_params_pushed(3);
}
#endif
