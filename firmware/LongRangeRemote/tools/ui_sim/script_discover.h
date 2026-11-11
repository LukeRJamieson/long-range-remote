#define CHECK(c) do { if (!(c)) { printf("CHECK FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int fails = 0;
int main() {
  ui::setNetworkInfo("192.168.4.1", "", "");
  discover::begin();
  ui::begin(); settings::setActive(30); run(50);
  press('B'); seq("DDD"); shot("menu_discover");
  press('S'); shot("families");
  seq("D"); press('S'); run(2500); shot("sweep_nec");            // NEC only
  CHECK(discover::state().mode == discover::SWEEP && discover::state().pos >= 2);
  int at = discover::state().pos;
  press('S'); run(300); shot("narrow_trying"); run(1500); shot("narrow_tried");
  CHECK(discover::state().mode == discover::NARROW && discover::state().pos == at - 3 + 0);
  press('R'); run(1200); CHECK(discover::state().pos == at - 2);
  press('S', 900); run(600); shot("learn_first");                 // hold OK: this one
  CHECK(discover::state().mode == discover::LEARN);
  int s0 = sim.sends; press('R'); run(400); CHECK(sim.sends == s0 + 1); shot("learn_next");
  press('S', 900); shot("name_list");
  press('S'); shot("learn_named"); CHECK(discover::state().n_learned == 1);
  press('R'); run(300); press('S', 900); seq("DDD"); press('S'); CHECK(discover::state().n_learned == 2);
  press('R'); run(300); press('S', 900); press('U'); shot("name_list_end"); press('S'); CHECK(discover::state().n_learned == 3);
  shot("learn_custom");
  press('B'); shot("done");
  press('S'); shot("saved"); CHECK(sim.restart && sim.saved_text.find("protocol: NEC") != std::string::npos);
  printf("%s", sim.saved_text.c_str());
  // all protocols, skip, stop
  sim.restart = false; discover::stop(); run(100); shot("after_stop_menu");
  seq("DDD"); press('S'); press('S'); run(1500); shot("sweep_all"); press('R'); run(600); shot("sweep_all_skipped");
  CHECK(discover::state().fam >= 1);
  press('B'); shot("stopped_to_families"); CHECK(discover::state().mode == discover::IDLE);
  printf(fails ? "SOME CHECKS FAILED\n" : "checks ok\n");
  return 0;
}
