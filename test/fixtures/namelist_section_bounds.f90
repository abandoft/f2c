program namelist_section_bounds
  implicit none

  integer :: anchored(-2:6)
  integer :: descending(-2:6)
  integer :: plane(-1:1, 2:6)
  integer :: anchored_snapshot(-2:6)
  integer :: descending_snapshot(-2:6)
  integer :: plane_snapshot(-1:1, 2:6)
  integer :: status
  integer :: i
  integer :: j
  character(512) :: record
  namelist /sample/ anchored, descending, plane

  anchored = -1
  descending = -1
  plane = -1
  record = '&sample anchored(::3)=12,11,14, descending(::-3)=66,33,7, ' // &
           'plane(::2,::-2)=16,36,14,34,12,32 /'
  read(record, nml=sample, iostat=status)

  if (status /= 0) stop 1
  if (anchored(-2) /= 12 .or. anchored(1) /= 11 .or. anchored(4) /= 14) stop 2
  do i = -2, 6
    if (mod(i + 2, 3) /= 0 .and. anchored(i) /= -1) stop 3
  end do
  if (descending(6) /= 66 .or. descending(3) /= 33 .or. descending(0) /= 7) stop 4
  do i = -2, 6
    if (mod(6 - i, 3) /= 0 .and. descending(i) /= -1) stop 5
  end do
  if (plane(-1, 6) /= 16 .or. plane(1, 6) /= 36) stop 6
  if (plane(-1, 4) /= 14 .or. plane(1, 4) /= 34) stop 7
  if (plane(-1, 2) /= 12 .or. plane(1, 2) /= 32) stop 8
  do j = 2, 6
    do i = -1, 1
      if ((i == 0 .or. mod(j, 2) /= 0) .and. plane(i, j) /= -1) stop 9
    end do
  end do

  anchored_snapshot = anchored
  descending_snapshot = descending
  plane_snapshot = plane
  record = '&sample anchored(-2:9:3)=1,2,3,4, descending=0, plane=0 /'
  read(record, nml=sample, iostat=status)
  if (status == 0) stop 10
  if (any(anchored /= anchored_snapshot)) stop 11
  if (any(descending /= descending_snapshot)) stop 12
  if (any(plane /= plane_snapshot)) stop 13
end program namelist_section_bounds
