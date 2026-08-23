program allocation_model_rich
    implicit none
    type :: box
        integer, allocatable :: values(:)
    end type box

    character(len=4) :: words(3)
    character(len=:), allocatable :: first(:), second(:)
    type(box) :: templates(2)
    type(box), allocatable :: copies(:), again(:)

    words(1) = 'a'
    words(2) = 'bb'
    words(3) = 'ccc'
    allocate(first, second, source=words(3:1:-1))
    if (len(first) /= 4 .or. size(first) /= 3) error stop 1
    if (first(1) /= 'ccc' .or. first(2) /= 'bb' .or. first(3) /= 'a') error stop 2
    if (any(second /= first)) error stop 3

    allocate(templates(1)%values, source=[1, 2, 3])
    allocate(templates(2)%values, source=[4, 5])
    allocate(copies, again, source=templates(2:1:-1))
    if (size(copies) /= 2) error stop 4
    if (size(copies(1)%values) /= 2) error stop 8
    if (any(copies(1)%values /= [4, 5])) error stop 5
    if (any(copies(2)%values /= [1, 2, 3])) error stop 6
    templates(2)%values(1) = 99
    if (copies(1)%values(1) /= 4 .or. again(1)%values(1) /= 4) error stop 7

    deallocate(first, second, copies, again)
    deallocate(templates(1)%values, templates(2)%values)
end program allocation_model_rich
