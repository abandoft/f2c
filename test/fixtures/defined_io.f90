module defined_io_types
  implicit none

  type :: payload
    integer :: value
  contains
    procedure :: read_formatted => payload_read_formatted
    procedure :: write_formatted => payload_write_formatted
    procedure :: read_unformatted => payload_read_unformatted
    procedure :: write_unformatted => payload_write_unformatted
    generic :: read(formatted) => read_formatted
    generic :: write(formatted) => write_formatted
    generic :: read(unformatted) => read_unformatted
    generic :: write(unformatted) => write_unformatted
  end type payload

contains

  subroutine payload_write_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(payload), intent(in) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg
    integer :: offset

    offset = 0
    if (iotype(1:2) == 'DT') offset = v_list(1)
    write(unit, '(i0)', iostat=iostat, iomsg=iomsg) dtv%value + offset
  end subroutine payload_write_formatted

  subroutine payload_read_formatted(dtv, unit, iotype, v_list, iostat, iomsg)
    class(payload), intent(inout) :: dtv
    integer, intent(in) :: unit
    character(*), intent(in) :: iotype
    integer, intent(in) :: v_list(:)
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    read(unit, '(i2)', iostat=iostat, iomsg=iomsg) dtv%value
    if (iotype(1:2) == 'DT') dtv%value = dtv%value - v_list(1)
  end subroutine payload_read_formatted

  subroutine payload_write_unformatted(dtv, unit, iostat, iomsg)
    class(payload), intent(in) :: dtv
    integer, intent(in) :: unit
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    write(unit, iostat=iostat, iomsg=iomsg) dtv%value
  end subroutine payload_write_unformatted

  subroutine payload_read_unformatted(dtv, unit, iostat, iomsg)
    class(payload), intent(inout) :: dtv
    integer, intent(in) :: unit
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg

    read(unit, iostat=iostat, iomsg=iomsg) dtv%value
  end subroutine payload_read_unformatted
end module defined_io_types

program defined_io
  use defined_io_types
  implicit none
  type(payload) :: source, restored
  character(32) :: buffer

  source%value = 42
  buffer = ''
  write(buffer, "(DT'offset'(7))") source
  if (buffer(1:2) /= '49') stop 1

  buffer = '57'
  read(buffer, "(DT'offset'(7))") restored
  if (restored%value /= 50) stop 2

  buffer = ''
  write(buffer, *) source
  if (buffer(2:3) /= '42') stop 3

  open(unit=27, status='scratch', form='unformatted')
  write(27) source
  rewind(27)
  restored%value = 0
  read(27) restored
  close(27)
  if (restored%value /= 42) stop 4
end program defined_io
