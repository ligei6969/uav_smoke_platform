#include "smoke/target_selector.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks=0;
void Check(bool value, const char *message)
{
    ++checks;
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
smoke::BoundingBox A(double confidence=.9) { return {10,10,30,30,confidence}; }
smoke::BoundingBox B(double confidence=.7) { return {60,10,80,30,confidence}; }
smoke::FrameStamp Frame(std::uint64_t id, double time=-1)
{
    smoke::FrameStamp f;
    f.source="camera"; f.frame_id=id; f.time_sec=time<0 ? id*.1 : time;
    f.lens=smoke::Lens::Unknown; f.time_base=smoke::TimeBase::Replay;
    return f;
}
smoke::TargetSelection Update(smoke::TargetSelector &s, std::uint64_t id,
                              const std::vector<smoke::BoundingBox> &boxes)
{
    const auto f=Frame(id);
    return s.Update(f,100,100,boxes,f.time_sec);
}
void Lock(smoke::TargetSelector &s)
{
    Check(Update(s,0,{A(),B()}).selected_index==-1,"first acquisition must wait");
    Check(Update(s,1,{A(),B()}).selected_index==-1,"second acquisition must wait");
    const auto r=Update(s,2,{A(),B()});
    Check(r.selected_index==0 && r.target_changed && r.reason=="target_acquired","acquisition confirmation");
}
void TestSwitchHysteresis()
{
    smoke::TargetSelector s; Lock(s);
    for (std::uint64_t i=3;i<11;++i) {
        const auto r=Update(s,i,{A(i%2 ? .6 : .8),B(.9)});
        Check(r.selected_index==0 && !r.target_changed,"alternating confidence must not switch");
    }
    Check(Update(s,11,{A(.6),B(.95)}).selected_index==0,"challenger confirmation one");
    Check(Update(s,12,{A(.6),B(.95)}).selected_index==0,"challenger confirmation two");
    auto r=Update(s,13,{A(.6),B(.95)});
    Check(r.selected_index==1 && r.target_changed && r.reason=="target_switched","confirmed challenger switches");
    r=Update(s,14,{B(.4),A(.5)});
    Check(r.selected_index==0 && !r.target_changed,"target follows IoU rather than vector index");
}
void TestMissingAndReacquisition()
{
    smoke::TargetSelector s; Lock(s);
    auto r=Update(s,3,{});
    Check(r.selected_index==-1 && r.reason=="target_missing","empty frame never returns old target");
    r=Update(s,4,{B(.95)});
    Check(r.selected_index==-1 && !r.target_changed,"unrelated target cannot steal missing lock");
    r=Update(s,5,{});
    Check(r.selected_index==-1 && r.target_changed && r.reason=="target_released","lost lock released");
    Check(Update(s,6,{B()}).selected_index==-1,"new target needs confirmation");
    Check(Update(s,7,{B()}).selected_index==-1,"new target second frame");
    Check(Update(s,8,{B()}).selected_index==0,"new target acquired after release");
    smoke::TargetSelector short_loss; Lock(short_loss);
    Update(short_loss,3,{});
    r=Update(short_loss,4,{A(.4)});
    Check(r.selected_index==0 && !r.target_changed,"brief occlusion preserves geometric lock");
}
void TestFreshnessAndOrder()
{
    smoke::TargetSelector s; Lock(s);
    Check(Update(s,2,{B(.99)}).reason=="stale_frame","duplicate frame rejected");
    Check(s.Update(Frame(1),100,100,{B(.99)},.2).reason=="stale_frame","old frame rejected");
    auto stamp=Frame(3,.1);
    Check(s.Update(stamp,100,100,{B(.99)},.3).reason=="out_of_order_time","old timestamp rejected");
    Check(Update(s,3,{A()}).selected_index==0,"rejected frame does not mutate lock");
    stamp=Frame(4);
    const auto expired=s.Update(stamp,100,100,{A()},1.0);
    Check(expired.selected_index==-1 && expired.target_changed && expired.reason=="target_expired","aged frame releases target");
    smoke::TargetSelector gap; Lock(gap);
    auto r=gap.Update(Frame(11),100,100,{A()},1.1);
    Check(r.reason=="target_released" && r.selected_index==-1,"long unobserved gap releases even same box");
    smoke::TargetSelector skipped;
    Check(Update(skipped,0,{A()}).selected_index==-1,"start confirmation");
    Check(Update(skipped,2,{A()}).selected_index==-1,"frame gap breaks consecutive confirmation");
    Check(Update(skipped,3,{A()}).selected_index==-1,"gap starts new confirmation window");
    Check(Update(skipped,4,{A()}).selected_index==0,"new consecutive window acquires");
}
void TestReconnectAndReset()
{
    smoke::TargetSelector s; Lock(s);
    auto stamp=Frame(0,.3); stamp.stream_epoch=1;
    auto r=s.Update(stamp,100,100,{B()},.3);
    Check(r.selected_index==-1 && r.target_changed,"reconnect clears old lock");
    stamp=Frame(1,.4); stamp.stream_epoch=1; s.Update(stamp,100,100,{B()},.4);
    stamp=Frame(2,.5); stamp.stream_epoch=1;
    Check(s.Update(stamp,100,100,{B()},.5).selected_index==0,"new epoch acquires");
    auto old=Frame(99,.6);
    Check(s.Update(old,100,100,{A()},.6).reason=="stale_epoch","old epoch cannot return");
    s.Reset();
    Check(s.Update(stamp,100,100,{B()},.6).reason=="stale_frame","manual reset retains watermark");
    for (std::uint64_t id=3;id<6;++id) {
        stamp=Frame(id,(id+3)*.1); stamp.stream_epoch=1;
        r=s.Update(stamp,100,100,{B()},stamp.time_sec);
        Check(r.selected_index==(id==5 ? 0 : -1),"manual reset requires fresh acquisition");
    }
    stamp=Frame(0,.9); stamp.source="other";
    Check(s.Update(stamp,100,100,{A()},.9).selected_index==-1,"source switch clears lock");
    stamp=Frame(1,1.0); stamp.source="other"; stamp.lens=smoke::Lens::Zoom;
    Check(s.Update(stamp,100,100,{A()},1.0).selected_index==-1,"lens switch restarts confirmation");
    smoke::TargetSelector clock_restart; Lock(clock_restart);
    for (std::uint64_t id=0;id<3;++id) {
        stamp=Frame(id); stamp.stream_epoch=1;
        r=clock_restart.Update(stamp,100,100,{B()},stamp.time_sec);
        Check(r.selected_index==(id==2 ? 0 : -1),"epoch change allows a fresh replay clock");
    }
    stamp=Frame(0); stamp.source="fresh";
    Check(clock_restart.Update(stamp,100,100,{A()},0).reason=="acquiring","new source allows fresh clock");
}
void TestInvalidInputsAndBounds()
{
    smoke::TargetSelector s; Lock(s);
    std::vector<smoke::BoundingBox> too_many(301,A());
    auto r=Update(s,3,too_many);
    Check(r.reason=="candidate_limit" && r.target_changed,"candidate count bounded and lock released");
    smoke::TargetSelector exact;
    Check(Update(exact,0,std::vector<smoke::BoundingBox>(300,A())).reason=="acquiring","exact capacity accepted");
    auto bad=A(); bad.x2=101;
    Check(Update(exact,1,{bad}).reason=="invalid_candidate","out-of-image box rejected");
    bad=A(); bad.confidence=std::numeric_limits<double>::quiet_NaN();
    Check(Update(exact,1,{bad}).reason=="invalid_candidate","NaN confidence rejected");
    Check(exact.Update(Frame(1),1,100,{},.1).reason=="invalid_frame","dimensions validated even without boxes");
    auto stamp=Frame(1); stamp.lens=static_cast<smoke::Lens>(99);
    Check(exact.Update(stamp,100,100,{},.1).reason=="invalid_frame","invalid lens enum rejected");
    Check(exact.Update(Frame(2),100,100,{A()},.1).reason=="future_frame","future target rejected");
    smoke::TargetSelector reverse; Lock(reverse);
    Check(reverse.Update(Frame(3),100,100,{A()},.1).reason=="clock_reversed","now clock reversal rejected");
    auto rejected=[](smoke::TargetSelectorConfig c) {
        try { smoke::TargetSelector selector(c); return false; }
        catch (const std::invalid_argument &) { return true; }
    };
    smoke::TargetSelectorConfig c;
    c.max_candidates=301; Check(rejected(c),"config candidate bound");
    c=smoke::TargetSelectorConfig(); c.confirmation_frames=0; Check(rejected(c),"config confirmation bound");
    c=smoke::TargetSelectorConfig(); c.keep_confidence=.9; Check(rejected(c),"config threshold order");
    c=smoke::TargetSelectorConfig(); c.association_iou=0; Check(rejected(c),"config IoU must associate");
    c=smoke::TargetSelectorConfig(); c.max_lost_sec=std::numeric_limits<double>::infinity(); Check(rejected(c),"config finite durations");
    c=smoke::TargetSelectorConfig(); c.max_lost_sec=0; Check(rejected(c),"config lost time must be positive");
}
}
int main()
{
    TestSwitchHysteresis(); TestMissingAndReacquisition(); TestFreshnessAndOrder();
    TestReconnectAndReset(); TestInvalidInputsAndBounds();
    std::cout << checks << " target-selection checks passed\n";
}
