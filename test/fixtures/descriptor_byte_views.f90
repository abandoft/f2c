module byte_views
  implicit none
  integer :: evaluations = 0
  type :: leaf
    real(8) :: value
    character(5) :: label
    integer :: markers(2)
  end type
  type :: record
    integer :: tag
    type(leaf) :: detail
  end type
  type :: phasor
    integer :: tag
    complex(8) :: phase
  end type
contains
  integer function bound(value)
    integer, intent(in) :: value
    evaluations = evaluations + 1
    bound = value
  end function

  subroutine retain_real(target, view)
    real(8), target, intent(inout) :: target(:)
    real(8), pointer, intent(out) :: view(:)
    view => target
  end subroutine

  subroutine retain_character(target, view)
    character(*), target, intent(inout) :: target(:)
    character(:), pointer, intent(out) :: view(:)
    view => target
  end subroutine

  subroutine retain_complex(target, view)
    complex(8), target, intent(inout) :: target(:)
    complex(8), pointer, intent(out) :: view(:)
    view => target
  end subroutine

  function text_view(target) result(view)
    character(*), target, intent(inout) :: target(:)
    character(:), pointer :: view(:)
    view => target
  end function

  subroutine contiguous_text(values)
    character(*), contiguous, intent(inout) :: values(:)
    values = 'YZ'
  end subroutine

  subroutine contiguous_complex(values)
    complex(8), contiguous, intent(inout) :: values(:)
    values = values + cmplx(10.0_8, 20.0_8, kind=8)
  end subroutine

  integer function read_text(values)
    character(*), intent(in) :: values(:)
    read_text = len(values) * size(values)
  end function

  subroutine run()
    type(record), target :: records(-1:2), grid(2,3)
    type(phasor), target :: phases(3)
    type(record), allocatable, target :: empty(:)
    real(8), pointer :: numbers(:), matrix(:,:)
    integer, pointer :: markers(:)
    character(:), pointer :: words(:)
    complex(8), pointer :: phase_values(:)
    character(0), target :: zero_width(3)
    integer :: i, j

    records%tag = 71
    records%detail%value = 1.0_8
    records%detail%label = 'abcde'
    do i = -1, 2
      records(i)%detail%markers = [1,2]
    end do
    call retain_real(records%detail%value, numbers)
    numbers(2) = 22.0_8
    if (records(0)%detail%value /= 22.0_8) stop 1
    if (sum(numbers) /= 25.0_8 .or. any(records%tag /= 71)) stop 2
    if (.not. associated(numbers, records%detail%value)) stop 3
    numbers => records(2:-1:-1)%detail%value
    numbers(1) = 44.0_8
    if (records(2)%detail%value /= 44.0_8 .or. sum(numbers) /= 68.0_8) stop 4

    evaluations = 0
    markers => records%detail%markers(bound(2))
    if (evaluations /= 1) stop 5
    markers(3) = 99
    if (records(1)%detail%markers(2) /= 99) stop 6
    if (any(records(-1)%detail%markers /= [1,2])) stop 7

    call retain_character(records%detail%label(2:3), words)
    if (len(words) /= 2 .or. size(words) /= 4) stop 8
    words(3) = 'XY'
    if (records(1)%detail%label /= 'aXYde') stop 9
    if (.not. associated(words, records%detail%label(2:3))) stop 10
    if (read_text(records%detail%label(2:3)) /= 8) stop 11
    evaluations = 0
    words => records(2:-1:-1)%detail%label(bound(2):bound(3))
    if (evaluations /= 2 .or. len(words) /= 2) stop 12
    words(2) = 'UV'
    if (records(1)%detail%label /= 'aUVde') stop 13
    words => text_view(records%detail%label(2:3))
    words(1) = 'QR'
    if (records(-1)%detail%label /= 'aQRde') stop 14
    call contiguous_text(records(2:-1:-1)%detail%label(2:3))
    if (any(records%detail%label /= 'aYZde')) stop 15

    do j = 1, 3
      do i = 1, 2
        grid(i,j)%detail%value = real(10*j+i,kind=8)
      end do
    end do
    matrix => grid%detail%value
    if (any(shape(matrix) /= [2,3])) stop 16
    matrix(2,3) = 123.0_8
    if (grid(2,3)%detail%value /= 123.0_8) stop 17
    if (.not. associated(matrix, grid%detail%value)) stop 18
    if (sum(matrix) /= 220.0_8 .or. sum(grid%detail%value) /= 220.0_8) stop 29
    if (.not. all(grid%detail%value > 0.0_8)) stop 30
    matrix => grid(2:1:-1,3:1:-1)%detail%value
    matrix(2,2) = 321.0_8
    if (grid(1,2)%detail%value /= 321.0_8) stop 19
    if (sum(matrix) /= 520.0_8) stop 31

    phases%tag = 82
    phases%phase = cmplx(1.0_8,2.0_8,kind=8)
    call retain_complex(phases%phase, phase_values)
    phase_values(2) = cmplx(3.0_8,4.0_8,kind=8)
    if (sum(phase_values) /= cmplx(5.0_8,8.0_8,kind=8)) stop 20
    if (phases(2)%phase /= cmplx(3.0_8,4.0_8,kind=8)) stop 21
    call contiguous_complex(phases(3:1:-1)%phase)
    if (sum(phases%phase) /= cmplx(35.0_8,68.0_8,kind=8)) stop 22
    if (any(phases%tag /= 82)) stop 23

    allocate(empty(0))
    numbers => empty%detail%value
    if (size(numbers) /= 0) stop 24
    if (associated(numbers, empty%detail%value)) stop 25
    numbers => records(99:1)%detail%value
    if (size(numbers) /= 0) stop 27
    words => records(99:1)%detail%label(100:-100)
    if (size(words) /= 0 .or. len(words) /= 0) stop 28
    call retain_character(zero_width, words)
    if (size(words) /= 3 .or. len(words) /= 0) stop 26
    nullify(numbers, words, markers, matrix, phase_values)
    deallocate(empty)
  end subroutine
end module
