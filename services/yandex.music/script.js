
(function(plugin){
  plugin.actions = {
    play:  function(){
        $(".player-controls__btn_play").click();
    },
    pause: function(){
        $(".player-controls__btn_pause").click();
    },
    next:  function(){
        $(".player-controls__btn_next").click();
    },
    clearPage: function(){
        $(".overhead__content").remove();
        $(".overhead").remove();
        $(".sidebar__placeholder").remove();
    },
  }
  
  plugin.QObject.addAction("play",  "media-playback-start", "play");
  plugin.QObject.addAction("pause", "media-playback-pause", "pause");
  plugin.QObject.addAction("next",  "media-skip-forward",   "next");
  
  
}(WebMusicService));
