module namelist_dtio_unit_types
  implicit none

  integer :: observed_read_unit = -1
  integer :: observed_write_unit = -1

  type :: wrapped_integer
    integer :: value = 0
  contains
    procedure :: read_formatted => wrapped_read_formatted
    procedure :: write_formatted => wrapped_write_formatted
    generic :: read(formatted) => read_formatted
    generic :: write(formatted) => write_formatted
  end type wrapped_integer

contains

  subroutine wrapped_read_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(wrapped_integer), intent(inout) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    observed_read_unit = unit
    if (iotype /= 'NAMELIST' .or. size(v_list) /= 0) then
      iostat = 1
      return
    end if
    read(unit, *, iostat=iostat, iomsg=iomsg) dtv%value
  end subroutine wrapped_read_formatted

  subroutine wrapped_write_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(wrapped_integer), intent(in) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    observed_write_unit = unit
    if (iotype /= 'NAMELIST' .or. size(v_list) /= 0) then
      iostat = 1
      return
    end if
    write(unit, *, iostat=iostat, iomsg=iomsg) dtv%value
  end subroutine wrapped_write_formatted
end module namelist_dtio_unit_types

program namelist_dtio_unit
  use namelist_dtio_unit_types
  implicit none

  type(wrapped_integer) :: item
  integer :: sentinel
  integer :: status
  namelist /sample/ item, sentinel

  item%value = 73
  sentinel = 9
  open(unit=42, status='scratch', form='formatted', action='readwrite', iostat=status)
  if (status /= 0) stop 1
  write(42, nml=sample, iostat=status)
  if (status /= 0) stop 2
  if (observed_write_unit /= 42) stop 8
  rewind(42, iostat=status)
  if (status /= 0) stop 3
  item%value = 0
  sentinel = 0
  read(42, nml=sample, iostat=status)
  if (status /= 0) stop 4
  close(42, iostat=status)
  if (status /= 0) stop 5
  if (observed_read_unit /= 42) stop 6
  if (item%value /= 73 .or. sentinel /= 9) stop 7
  write(*, '(A,I0,A,I0,A,I0)') 'read-unit=', observed_read_unit, &
                               ',write-unit=', observed_write_unit, ',value=', item%value
end program namelist_dtio_unit
