program allocation_model_guard
    implicit none
    integer, allocatable :: missing, target
    integer :: status
    character(len=48) :: message

    allocate(target, source=missing, stat=status, errmsg=message)
    if (status == 0 .or. allocated(target)) error stop 1
end program allocation_model_guard
