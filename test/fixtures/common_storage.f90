program common_storage
  implicit none
  integer :: values(2), total, scale
  character(len=4) :: label
  common values, total /control/ scale
  common // label

  call populate_common(6)
  write (*, '(4(I0,1X),A)') values(1), values(2), total, scale, label
end program common_storage

subroutine populate_common(seed)
  implicit none
  integer, intent(in) :: seed
  integer :: entries(2), sum, factor
  character(len=4) :: text
  common entries, sum /control/ factor
  common // text

  entries = (/ seed, seed + 3 /)
  sum = entries(1) + entries(2)
  factor = sum * 2
  text = 'done'
end subroutine populate_common
