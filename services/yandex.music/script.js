
(function(plugin){
  plugin.actions = {
    clearPage: function(){
        $(".overhead__content").remove();
        $(".overhead").remove();
        $(".sidebar__placeholder").remove();
    },
    gogogo: function(){
        alert("gogogo!");
    }
  }
  
  plugin.QObject.playSignal.connect(function(){
    $(".player-controls__btn_play").click();
  });
  
  plugin.QObject.pauseSignal.connect(function(){
    $(".player-controls__btn_pause").click();
  });
  
  plugin.QObject.nextSignal.connect(function(){
    $(".player-controls__btn_next").click();
  });
  
  plugin.QObject.addCustomAction("gogogo", "click ME",  "edit-undo");
  
  plugin.QObject.removeAction("stop");

  
  
}(WebMusicService));
