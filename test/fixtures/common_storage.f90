program common_storage
  implicit none
  integer :: values(2), total, scale
  real :: real_view(2)
  character(len=4) :: label
  integer :: zero_array(0), zero_value
  character(len=0) :: zero_text
  common values, total /control/ scale
  common // label
  common /overlay/ real_view
  common /zero_storage/ zero_array, zero_text, zero_value

  call populate_common(6)
  call populate_overlay()
  call populate_zero_storage()
  write (*, '(4(I0,1X),A)') values(1), values(2), total, scale, label
  write (*, '(2(F0.1,1X))') real_view(1), real_view(2)
  write (*, '(3(I0,1X))') zero_value, size(zero_array), len(zero_text)
end program common_storage

subroutine populate_common(seed)
  implicit none
  integer, intent(in) :: seed
  integer :: first, second, sum, factor
  character(len=4) :: text
  common first, second, sum /control/ factor
  common // text

  first = seed
  second = seed + 3
  sum = first + second
  factor = sum * 2
  text = 'done'
end subroutine populate_common

subroutine populate_overlay()
  implicit none
  integer :: words(2)
  common /overlay/ words

  words = (/ 1065353216, 1073741824 /)
end subroutine populate_overlay

subroutine populate_zero_storage()
  implicit none
  integer :: empty(1:0), value
  character(len=0) :: text
  common /zero_storage/ empty, text, value

  value = 42
end subroutine populate_zero_storage
