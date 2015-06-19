
(function(plugin){
  plugin.name = "Yandex Music";
  plugin.url = "https://music.yandex.ru/";

  plugin.actions = {
    play:  function(){alert("play music!");},
    stop:  function(){alert("stop music!")},
    pause: function(){alert("pause music!")},
    next:  function(){alert("next music!")},
    clearPage: function(){},
  }
  
  plugin.QObject.addAction("lets rock!",  "media-playback-start", "play");
  plugin.QObject.addAction("click me to stop",  "media-playback-stop",  "stop");
  plugin.QObject.addAction("pause", "media-playback-pause", "pause");
  plugin.QObject.addAction("next",  "media-skip-forward",   "next");
  
  
}(WebMusicPlugin));
