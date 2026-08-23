program allocation_model
    implicit none
    integer, allocatable :: first(:), second(:), section(:), shaped(:), packed(:), result(:)
    integer :: values(5), undefined(3)
    integer :: calls

    values = [1, 2, 3, 4, 5]
    calls = 0

    allocate(first, second, source=values + 10)
    if (any(first /= [11, 12, 13, 14, 15])) error stop 1
    if (any(second /= first)) error stop 2

    allocate(section, source=values(5:1:-2))
    if (lbound(section, 1) /= 1 .or. ubound(section, 1) /= 3) error stop 3
    if (any(section /= [5, 3, 1])) error stop 4

    allocate(shaped(1:4), source=next_value())
    if (calls /= 1 .or. any(shaped /= 17)) error stop 5

    allocate(packed, source=pack(values, values > 2))
    if (any(packed /= [3, 4, 5])) error stop 6

    allocate(result, source=make_values())
    if (calls /= 2 .or. any(result /= [21, 22, 23])) error stop 7

    deallocate(first, second)
    allocate(first, mold=undefined(3:1:-1))
    if (size(first) /= 3 .or. lbound(first, 1) /= 1) error stop 8

    deallocate(first, section, shaped, packed, result)
contains
    integer function next_value()
        calls = calls + 1
        next_value = 17
    end function next_value

    function make_values() result(output)
        integer :: output(3)
        calls = calls + 1
        output = [21, 22, 23]
    end function make_values
end program allocation_model
