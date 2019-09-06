struct TWD1772Dpll {

  int GetNextBit(int &tm, BYTE drive);
  void Reset(int when);
  void SetClock(const int &period);
  int latest_transition;
  int ctime;
  int delays[42];
  int write_start_time;
  int write_buffer[32];
  int write_position;
  WORD counter;
  WORD increment;
  WORD transition_time;
  BYTE history;
  BYTE slot;
  BYTE phase_add, phase_sub, freq_add, freq_sub;

};
#if defined(SSE_WD1772_BIT_LEVEL)

int TWD1772Dpll::GetNextBit(int &tm, BYTE drive) {
  ASSERT(drive<=1);
  ASSERT(SF314[drive].ImageType.Extension==EXT_SCP);

  int aa=0;

  BYTE timing_in_us;

  while(ctime-latest_transition>=0)
  {
#if defined(SSE_DISK_SCP) // add formats here ;)
    aa=ImageSCP[drive].GetNextTransition(timing_in_us); // cycles to next 1
#endif
    TRACE_MFM("(%d)",timing_in_us);

    latest_transition+=aa;
  }
  int when=latest_transition;

  ASSERT(!(when==-1 || when-ctime<0));

  for(;;) {
    int etime = ctime+delays[slot];

    if(transition_time == 0xffff && etime-when >= 0)
      transition_time = counter;

    if(slot < 8) { //SS I don't understand this, why only <8?
      BYTE mask = 1 << slot;
      if(phase_add & mask)
        counter += 226;
      else if(phase_sub & mask)
        counter += 30;
      else
        counter += increment;

      if((freq_add & mask) && increment < 140)
        increment++;
      else if((freq_sub & mask) && increment > 117)
        increment--;
    } else
      counter += increment;

    slot++;
    tm = etime;
    if(counter & 0x800)
      break;
  }

  int bit = transition_time != 0xffff;
  
  //if(transition_time != 0xffff) {
  if(bit) { //SS refactoring!
    static const BYTE pha[8] = { 0xf, 0x7, 0x3, 0x1, 0, 0, 0, 0 };
    static const BYTE phs[8] = { 0, 0, 0, 0, 0x1, 0x3, 0x7, 0xf };
    static const BYTE freqa[4][8] = {
      { 0xf, 0x7, 0x3, 0x1, 0, 0, 0, 0 },
      { 0x7, 0x3, 0x1, 0, 0, 0, 0, 0 },
      { 0x7, 0x3, 0x1, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 }
    };
    static const BYTE freqs[4][8] = {
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0x1, 0x3, 0x7 },
      { 0, 0, 0, 0, 0, 0x1, 0x3, 0x7 },
      { 0, 0, 0, 0, 0x1, 0x3, 0x7, 0xf },
    };

    int cslot = transition_time >> 8;
    ASSERT( cslot<8 );
    phase_add = pha[cslot];
    phase_sub = phs[cslot];
    int way = transition_time & 0x400 ? 1 : 0;
    if(history & 0x80)
      history = way ? 0x80 : 0x83;
    else if(history & 0x40)
      history = way ? history & 2 : (history & 2) | 1;
    freq_add = freqa[history & 3][cslot];
    freq_sub = freqs[history & 3][cslot];
    history = way ? (history >> 1) | 2 : history >> 1;
  } else
    phase_add = phase_sub = freq_add = freq_sub = 0; //SS don't understand this either

  counter &= 0x7ff;
  ctime = tm;
  transition_time = 0xffff;
  slot = 0;
  ASSERT( bit==0 || bit==1 );
  return bit;
}


void TWD1772Dpll::Reset(int when) {
  counter = 0;
  increment = 128;
  transition_time = 0xffff;
  history = 0x80;
  slot = 0;
  latest_transition= ctime = when;
  phase_add = 0x00;
  phase_sub = 0x00;
  freq_add  = 0x00;
  freq_sub  = 0x00;
  write_position = 0;
  write_start_time = -1;
  SetClock(1); // clock WD1772 = clock CPU, 16 cycles = 2 microseconds
}


void TWD1772Dpll::SetClock(const int &period)
{
  for(int i=0; i<42; i++)
    delays[i] = period*(i+1);
}

#endif//bit-leve


struct  TImageSCP:public TImageMfm {
  // interface (the same as for STW disk images)
  bool Open(char *path);
  void Close();
  bool LoadTrack(BYTE side,BYTE track,bool reload=false);
  WORD GetMfmData(WORD position); 
  void SetMfmData(WORD position, WORD mfm_data);
  int GetNextTransition(BYTE& us_to_next_flux);
  // other functions
  TImageSCP();
  ~TImageSCP();
  void ComputePosition();
  int UnitsToNextFlux(DWORD position);
  int UsToNextFlux(int units_to_next_flux);
#if !defined(SSE_WD1772_BIT_LEVEL)||defined(SSE_DISK_SCP_TO_MFM_PREVIEW)
  BYTE GetDelay(int position);
#endif
  void IncPosition();
  void Init();
#if defined(SSE_BOILER) && defined(SSE_DISK_SCP_TO_MFM_PREVIEW)
  void InterpretFlux(); // was a dev step
#endif
  // variables
  DWORD *TimeFromIndexPulse; // from IP
  DWORD nBits;
  WORD nBytes;
  TSCP_file_header file_header;
  TSCP_track_header track_header;
  BYTE rev;
};


void  TImageSCP::ComputePosition() {
  // when we start reading/writing, where on the disk?
  
  ASSERT(TimeFromIndexPulse);
  if(!TimeFromIndexPulse)
    return; //safety
  
  int cycles=time_of_next_event-SF314[DRIVE].time_of_last_ip;// CPU cycles since IP
  DWORD units=cycles*5; // in SCP units
  
  Position=0;
  for(DWORD i=0;i<nBits;i++) // slow search
  {
    if(TimeFromIndexPulse[i]>=units)
    {
      Position=i; // can be 0
      break;
    }
  }
#if defined(SSE_DISK_SCP_RANDOMISE)
/* Randomise first bit when starting reading a track.
   Fixes War Heli track 68
   The protection is looking for A1 47 4D 43 then checks that the byte
   before A1 isn't twice the same.
   Using rand(), it still may fail.
   TODO test if it breaks other SCP games?
*/
  if(!Position)
    Position=rand()%4; // IP + non-synchronisation
#endif

#if defined(SSE_WD1772_BIT_LEVEL)
  WD1772.Dpll.Reset(ACT); 
#endif

  ASSERT(SF314[DRIVE].CyclesPerByte());
  // just informative? TODO
  Disk[DRIVE].current_byte=(time_of_next_event-SF314[DRIVE].time_of_last_ip)
    /SF314[DRIVE].CyclesPerByte();
}

#if !defined(SSE_WD1772_BIT_LEVEL)||defined(SSE_DISK_SCP_TO_MFM_PREVIEW)

BYTE TImageSCP::GetDelay(int position) {
  // we want delay in ms, typically 4, 6, 8
  WORD units_to_next_flux=UnitsToNextFlux(position);
  BYTE delay_in_us;
  delay_in_us=UsToNextFlux(units_to_next_flux);
  return delay_in_us;    
}

#endif


int TImageSCP::UnitsToNextFlux(DWORD position) {
  // 1 unit = 25 nanoseconds = 1/40 ms
  ASSERT(position<nBits);
  ASSERT(position>=0);
  position=position%nBits; // safety
  DWORD time1=0,time2;
  if(position)
    time1=TimeFromIndexPulse[position-1];
  time2=TimeFromIndexPulse[position];
  ASSERT( time2>time1 );
  int units_to_next_flux=time2-time1; 
#if defined(SSE_DISK_SCP_DRIVE_WOBBLE) // this takes care of weak bits (?)
  int wobble=(rand()%4)-2; // a bit simplistic? 
  units_to_next_flux+=wobble;
#endif
  return units_to_next_flux;    
}


int TImageSCP::UsToNextFlux(int units_to_next_flux) {
  BYTE us_to_next_flux;
  BYTE ref_us= ((units_to_next_flux/40)+1)&0xFE;  // eg 4
  WORD ref_units = ref_us * 40;
  if(units_to_next_flux<ref_units-SCP_DATA_WINDOW_TOLERANCY)
    us_to_next_flux=ref_us-1;
  else if (units_to_next_flux>ref_units+SCP_DATA_WINDOW_TOLERANCY)
    us_to_next_flux=ref_us+1;
  else
    us_to_next_flux=ref_us;
  return us_to_next_flux;    
}


WORD TImageSCP::GetMfmData(WORD position) {
/*  We use the same interface for SCP as for STW so that integration
    with the Disk manager, WD1772 emu etc. is straightforward.
    But precise emulation doesn't send MFM data word by word (16bit).
    Instead it sends bytes and AM signals according to bit sequences,
    as analysed in (3rd party-inspired) WD1772.ShiftBit().
    note we need SSE_WD1772_BIT_LEVEL, we didn't keep beta code in v3.7.1
*/

  WORD mfm_data=0;

  if(!TimeFromIndexPulse) //safety, SCP track in ram?
    return mfm_data;

  // must compute new starting point?
  if(position!=0xFFFF)
    ComputePosition();

#if defined(SSE_WD1772_BIT_LEVEL)

  // we manage timing here, maybe we should do that in WD1772 instead
  int a1=WD1772.Dpll.ctime,a2,tm=0;

  // clear dsr signals
  WD1772.Amd.aminfo&=~(CAPSFDC_AI_DSRREADY|CAPSFDC_AI_DSRAM|CAPSFDC_AI_DSRMA1);

  // loop until break
  for(int i=0; ;i++) 
  {
    int bit=WD1772.Dpll.GetNextBit(tm,DRIVE); //tm isn't used...
    ASSERT(bit==0 || bit==1); // 0 or 1, clock and data
    TRACE_MFM("%d",bit); // full flow of bits
    if(WD1772.ShiftBit(bit)) // true if byte ready to transfer
      break;
  }//nxt i

  //WD1772.Mfm.data_last_bit=(mfm_data&1); // no use

  a2=WD1772.Dpll.ctime;
  int delay_in_cycles=(a2-a1);

#ifdef SSE_DEBUG  // only report DPLL if there's some adjustment
  if(WD1772.Dpll.increment!=128|| WD1772.Dpll.phase_add||WD1772.Dpll.phase_sub
    ||WD1772.Dpll.freq_add||WD1772.Dpll.freq_sub)
  {
    ASSERT( !(WD1772.Dpll.freq_add && WD1772.Dpll.freq_sub) ); 
    ASSERT( !(WD1772.Dpll.phase_add && WD1772.Dpll.phase_sub) );
    TRACE_MFM(" DPLL (%d,%d,%d) ",WD1772.Dpll.increment,WD1772.Dpll.phase_add-WD1772.Dpll.phase_sub,WD1772.Dpll.freq_add-WD1772.Dpll.freq_sub);
  }
  ASSERT(delay_in_cycles>0);
  TRACE_MFM(" %d cycles\n",delay_in_cycles);
#endif

  WD1772.update_time=time_of_next_event+delay_in_cycles; 

  if(WD1772.update_time-ACT<=0) // safety
  {
    TRACE_LOG("Argh! wrong disk timing %d ACT %d diff %d last IP %d pos %d/%d delay %d units %d\n",
      WD1772.update_time,ACT,ACT-WD1772.update_time,SF314[DRIVE].time_of_last_ip,Position,nBits-1,delay_in_cycles,TimeFromIndexPulse[Position-1]);
    WD1772.update_time=ACT+SF314[DRIVE].cycles_per_byte;
  }
#endif

  ASSERT(!mfm_data); // see note at top of function
  mfm_data=WD1772.Mfm.encoded; // correct?
  return mfm_data;
}
int TImageSCP::GetNextTransition(BYTE& us_to_next_flux) {
  int t=UnitsToNextFlux(Position);
  us_to_next_flux=UsToNextFlux(t); // in parameter
  IncPosition();
  t/=5; // in cycles
  return t; 
}
void TImageSCP::IncPosition() {
  ASSERT( Position>=0 );
  ASSERT( Position<nBits );
  Position++;
#if defined(SSE_DISK_SCP_394) //safer
  if(Position>=nBits)
#else
  if(Position==nBits)
#endif
  {
    Position=0;
    TRACE_FDC("\nSCP triggers IP side %d track %d rev %d/%d\n",
      CURRENT_SIDE,floppy_head_track[DRIVE],rev+1,file_header.IFF_NUMREVS);
    
/*  If a sector is spread over IP, we make sure that our event
    system won't start a new byte before returning to current
    byte. 
*/
    SF314[DRIVE].IndexPulse(true); // true for "image triggered"

    // provided there are >1 revs...    
    if(file_header.IFF_NUMREVS>1)
    {
      // we step revs 0->1 each IP, we'll reload 0 during rev
      // works with Turrican, I Ludicrus, Leavin' Teramis
      // Notice we do no computing, the first bit of the new rev
      // is relative to last bit of previous rev, or we are very
      // lucky.
      LoadTrack(CURRENT_SIDE,SF314[DRIVE].Track(),true);
    }      
  }
}


void TImageSCP::Init() {
  fCurrentImage=NULL;
  TimeFromIndexPulse=NULL;
  N_SIDES=2;
  N_TRACKS=83; //max
  nBytes=DISK_BYTES_PER_TRACK; //not really pertinent (TODO?)
}

void TImageSCP::InterpretFlux() {
  WORD current_mfm_word=0,amd=0,ndatabytes=0;
  int bit_counter=0;
  for(int i=0;i<nBits;i++)
  {
    BYTE delay_in_us=GetDelay(i);
#if defined(SSE_BOILER_TRACE_CONTROL)
    if(TRACE_MASK3&TRACE_CONTROL_FDCMFM)
      TRACE_LOG("%d ",delay_in_us);
#endif
    int n_shifts=(delay_in_us/2); // 4->2; 6->3; 8->4
    for(int j=0;j<n_shifts;j++)
    {
      if(j==n_shifts-1) // eg 001 3rd iteration we set bit
      {
        current_mfm_word|=1;
        amd|=1;
      }
      if(amd==0x4489 || amd==0x5224) 
      {
#if defined(SSE_BOILER_TRACE_CONTROL)
        if(TRACE_MASK3&TRACE_CONTROL_FDCMFM)
          TRACE_LOG("AM? %x ctr %d\n",amd,bit_counter);
#endif
        if(amd==0x4489)
        {
          amd=0; // no overlap
          current_mfm_word<<=15-bit_counter;
          bit_counter=15; //sync
        }
      }
      amd<<=1;
      bit_counter++;
      if(bit_counter==16)
      {
        WD1772.Mfm.encoded=current_mfm_word;
        WD1772.Mfm.Decode();
#if defined(SSE_BOILER_TRACE_CONTROL)
        if(TRACE_MASK3&TRACE_CONTROL_FDCMFM)
          TRACE_LOG("MFM %X amd %X C %X D %X \n",current_mfm_word,amd,WD1772.Mfm.clock,WD1772.Mfm.data);
#endif
        current_mfm_word=0;
        bit_counter=0;
        ndatabytes++;
      }
      current_mfm_word<<=1;
    }//j
  }//i
  TRACE_LOG("%d bytes on SCP track\n",ndatabytes);
}

#endif

