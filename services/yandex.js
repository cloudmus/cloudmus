
(function(plugin){
  plugin.name = "Yandex Music";
  plugin.url = "https://music.yandex.ru/";

  plugin.actions = {
    hello: function(arg1){console.log("Hello man!", arg1);},
    play:  function(){alert("play music!");},
    stop:  function(){alert("stop music!")},
    pause: function(){alert("pause music!")},
    next:  function(){alert("next music!")},
    clearPage: function(){},
  }
  
  plugin.QObject.addAction("play",  "media-playback-start", "play");
  plugin.QObject.addAction("stop",  "media-playback-stop",  "stop");
  plugin.QObject.addAction("pause", "media-playback-pause", "pause");
  plugin.QObject.addAction("next",  "media-skip-forward",   "next");
  
  
}(WebMusicPlugin));
