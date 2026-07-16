program derived_namelist
  implicit none
  type :: metrics
    integer :: samples(3)
    logical :: enabled
  end type metrics
  type :: report
    character(len=8) :: title
    type(metrics) :: data
  end type report
  type(report) :: item
  character(len=512) :: record
  namelist /state/ item

  item%title = 'summary'
  item%data%samples(1) = 4
  item%data%samples(2) = 8
  item%data%samples(3) = 15
  item%data%enabled = .true.
  write(record, nml=state)

  item%title = 'missing'
  item%data%samples(1) = 0
  item%data%samples(2) = 0
  item%data%samples(3) = 0
  item%data%enabled = .false.
  read(record, nml=state)

  if (item%title /= 'summary') stop 1
  if (item%data%samples(1) /= 4) stop 2
  if (item%data%samples(2) /= 8) stop 3
  if (item%data%samples(3) /= 15) stop 4
  if (.not. item%data%enabled) stop 5
end program derived_namelist
